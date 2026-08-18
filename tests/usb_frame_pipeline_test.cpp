#include "common/usb_frame_pipeline.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using swir::usb::CompletedFrame;
using swir::usb::FrameAssembler;
using swir::usb::FrameGeometry;
using swir::usb::RowPacketView;
using swir::usb::RowStreamParser;

void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::uint8_t> makePacket(const FrameGeometry &geometry,
                                     std::uint16_t frame,
                                     std::uint16_t row,
                                     std::uint16_t base)
{
    std::vector<std::uint8_t> packet(geometry.packetBytes(), 0);
    packet[0] = 0x90;
    packet[1] = 0xeb;
    packet[2] = 0x00;
    packet[3] = 0x00;
    packet[4] = std::uint8_t(geometry.width & 0xff);
    packet[5] = std::uint8_t(geometry.width >> 8);
    packet[6] = std::uint8_t(geometry.height & 0xff);
    packet[7] = std::uint8_t(geometry.height >> 8);
    packet[8] = std::uint8_t(row & 0xff);
    packet[9] = std::uint8_t(row >> 8);
    packet[10] = std::uint8_t(frame & 0xff);
    packet[11] = std::uint8_t(frame >> 8);
    packet[12] = 0xff;
    packet[13] = 0xff;
    packet[14] = 0x23;
    packet[15] = 0x01;
    for (std::size_t x = 0; x < geometry.width; ++x) {
        const std::uint16_t value = std::uint16_t(base + x);
        const std::size_t offset = geometry.headerBytes + x * 2;
        packet[offset] = std::uint8_t(value & 0xff);
        packet[offset + 1] = std::uint8_t(value >> 8);
    }
    return packet;
}

RowPacketView packetView(const FrameGeometry &geometry,
                         const std::vector<std::uint8_t> &packet)
{
    RowPacketView view;
    view.rowNumber = std::uint16_t(packet[8]) |
                     std::uint16_t(std::uint16_t(packet[9]) << 8);
    view.frameNumber = std::uint16_t(packet[10]) |
                       std::uint16_t(std::uint16_t(packet[11]) << 8);
    view.payload = packet.data() + geometry.headerBytes;
    view.payloadSize = geometry.payloadBytes();
    return view;
}

void testGeometryValidation()
{
    require(!FrameGeometry{}.isValid(), "空几何参数不应有效");
    require(!FrameGeometry{4, 3, 11, 2, 3}.isValid(),
            "不足12字节的包头不应有效");
    require(!FrameGeometry{4, 3, 16, 1, 3}.isValid(),
            "当前协议只允许16位像素");
    require(FrameGeometry{4, 3, 16, 2, 3}.isValid(),
            "合法几何参数被拒绝");
    FrameGeometry invalidTolerance{4, 3, 16, 2, 3};
    invalidTolerance.allowedMissingRows = 3;
    require(!invalidTolerance.isValid(), "不能允许整帧所有行均缺失");
}

void testEverySplitPosition()
{
    const FrameGeometry geometry{8, 4, 16, 2, 3};
    const auto packet = makePacket(geometry, 42, 3, 1000);
    for (std::size_t split = 0; split <= packet.size(); ++split) {
        RowStreamParser parser(geometry);
        std::vector<std::uint16_t> received;
        auto handler = [&received](const RowPacketView &view) {
            received.push_back(view.frameNumber);
            received.push_back(view.rowNumber);
        };
        require(parser.consume(packet.data(), split, handler),
                "第一段consume失败");
        require(parser.consume(packet.data() + split, packet.size() - split,
                               handler), "第二段consume失败");
        require(received == std::vector<std::uint16_t>({42, 3}),
                "跨块位置解析错误：" + std::to_string(split));
        require(parser.bufferedBytes() == 0, "完整包后仍残留字节");
    }
}

void testNoiseAndInvalidHeaderRecovery()
{
    const FrameGeometry geometry{4, 3, 16, 2, 3};
    auto invalid = makePacket(geometry, 7, 0, 10);
    const auto valid = makePacket(geometry, 8, 2, 20);
    std::vector<std::uint8_t> stream = {0x11, 0x22, 0x90};
    stream.insert(stream.end(), invalid.begin(), invalid.end());
    stream.insert(stream.end(), valid.begin(), valid.end());

    RowStreamParser parser(geometry);
    std::vector<std::uint16_t> frames;
    for (std::size_t offset = 0; offset < stream.size();) {
        const std::size_t chunk = std::min<std::size_t>(5, stream.size() - offset);
        parser.consume(stream.data() + offset, chunk,
                       [&frames](const RowPacketView &view) {
            frames.push_back(view.frameNumber);
        });
        offset += chunk;
    }
    require(frames == std::vector<std::uint16_t>({8}),
            "坏包后未恢复到有效行包");
    require(parser.stats().invalidHeaders == 1, "非法行号统计错误");
    require(parser.stats().packets == 1, "有效包统计错误");
    require(parser.stats().discardedBytes > 0, "噪声丢弃统计缺失");
}

void testFalseMagicInsidePixelPayload()
{
    const FrameGeometry geometry{32, 2, 16, 2, 3};
    auto first = makePacket(geometry, 20, 1, 100);
    const auto second = makePacket(geometry, 20, 2, 200);

    // 在像素区构造“魔数+看似合法行号”，但宽高和结束标记不合法。
    // 解析器从任意中间位置接入时必须跳过伪头并恢复到下一真实行头。
    const std::size_t falseMagic = geometry.headerBytes + 8;
    first[falseMagic + 0] = 0x90;
    first[falseMagic + 1] = 0xeb;
    first[falseMagic + 2] = 0x00;
    first[falseMagic + 3] = 0x00;
    first[falseMagic + 8] = 0x01;
    first[falseMagic + 9] = 0x00;

    std::vector<std::uint8_t> stream(first.begin() + falseMagic,
                                     first.end());
    stream.insert(stream.end(), second.begin(), second.end());
    RowStreamParser parser(geometry);
    std::vector<std::uint16_t> rows;
    parser.consume(stream.data(), stream.size(),
                   [&rows](const RowPacketView &view) {
        rows.push_back(view.rowNumber);
    });
    require(rows == std::vector<std::uint16_t>({2}),
            "像素区伪魔数破坏了下一真实行包");
    require(parser.stats().invalidHeaders >= 1,
            "像素区伪魔数没有计入无效头");
}

void testAssemblerOutOfOrderAndDuplicate()
{
    const FrameGeometry geometry{4, 3, 16, 2, 3};
    FrameAssembler assembler(geometry);
    auto row1 = makePacket(geometry, 100, 1, 100);
    auto row2 = makePacket(geometry, 100, 2, 200);
    auto row3 = makePacket(geometry, 100, 3, 300);

    require(!assembler.ingest(packetView(geometry, row2)),
            "一行数据不应完成帧");
    require(!assembler.ingest(packetView(geometry, row2)),
            "重复行不应完成帧");
    require(!assembler.ingest(packetView(geometry, row1)),
            "两行数据不应完成帧");
    auto completed = assembler.ingest(packetView(geometry, row3));
    require(completed.has_value(), "乱序三行没有组装成完整帧");
    require(completed->frameNumber == 100, "完整帧号错误");
    require(completed->pixels == std::vector<std::uint16_t>(
                {100, 101, 102, 103, 200, 201, 202, 203,
                 300, 301, 302, 303}), "完整帧像素顺序错误");
    const auto stats = assembler.stats();
    require(stats.acceptedRows == 3, "有效行统计错误");
    require(stats.duplicateRows == 1, "重复行统计错误");
    require(stats.completedFrames == 1, "完整帧统计错误");
}

void testSharedAssemblerFromFourEndpointThreads()
{
    const FrameGeometry geometry{16, 8, 16, 2, 3};
    FrameAssembler assembler(geometry);
    std::mutex resultMutex;
    std::vector<CompletedFrame> completed;
    std::vector<std::thread> workers;

    for (std::size_t endpoint = 0; endpoint < 4; ++endpoint) {
        workers.emplace_back([&, endpoint] {
            RowStreamParser parser(geometry);
            for (std::size_t row = endpoint + 1; row <= geometry.height; row += 4) {
                const auto packet = makePacket(geometry, 321,
                                               std::uint16_t(row),
                                               std::uint16_t(row * 100));
                const std::size_t split = 7 + endpoint;
                auto handler = [&](const RowPacketView &view) {
                    auto frame = assembler.ingest(view);
                    if (frame) {
                        std::lock_guard<std::mutex> lock(resultMutex);
                        completed.push_back(std::move(*frame));
                    }
                };
                parser.consume(packet.data(), split, handler);
                parser.consume(packet.data() + split,
                               packet.size() - split, handler);
            }
        });
    }
    for (auto &worker : workers) {
        worker.join();
    }

    require(completed.size() == 1, "四端点并发应只发布一个完整帧");
    require(completed.front().frameNumber == 321, "并发完整帧号错误");
    for (std::size_t row = 1; row <= geometry.height; ++row) {
        for (std::size_t x = 0; x < geometry.width; ++x) {
            const auto expected = std::uint16_t(row * 100 + x);
            require(completed.front().pixels[(row - 1) * geometry.width + x] ==
                        expected, "四端点并发像素错误");
        }
    }
}

void testFrameWindowAndLateRows()
{
    const FrameGeometry geometry{2, 2, 16, 2, 3};
    FrameAssembler assembler(geometry);
    auto frame10row1 = makePacket(geometry, 10, 1, 10);
    auto frame13row1 = makePacket(geometry, 13, 1, 30);
    auto frame10row2 = makePacket(geometry, 10, 2, 20);
    assembler.ingest(packetView(geometry, frame10row1));
    assembler.ingest(packetView(geometry, frame13row1));
    require(!assembler.ingest(packetView(geometry, frame10row2)),
            "窗口外旧帧不应完成");
    const auto stats = assembler.stats();
    require(stats.evictedFrames == 1, "三帧窗口淘汰统计错误");
    require(stats.expiredRows == 1, "迟到旧行统计错误");
}

void testWindowsCompatiblePartialFramePublishing()
{
    FrameGeometry geometry{2, 3, 16, 2, 3};
    geometry.allowedMissingRows = 1;
    FrameAssembler assembler(geometry);

    auto frame10row1 = makePacket(geometry, 10, 1, 100);
    auto frame10row2 = makePacket(geometry, 10, 2, 200);
    auto frame11row1 = makePacket(geometry, 11, 1, 110);
    auto frame11row3 = makePacket(geometry, 11, 3, 310);
    auto frame12row1 = makePacket(geometry, 12, 1, 120);

    require(!assembler.ingest(packetView(geometry, frame10row1)),
            "兼容模式不应在新帧到达前提前发布");
    require(!assembler.ingest(packetView(geometry, frame10row2)),
            "兼容模式不应在新帧到达前提前发布");
    auto first = assembler.ingest(packetView(geometry, frame11row1));
    require(first.has_value() && first->frameNumber == 10,
            "新帧到达后未发布缺一行的旧帧");
    require(first->missingRows == 1 &&
                first->rowsFilledFromPreviousFrame == 0,
            "首个兼容帧的补行统计错误");
    require(first->pixels == std::vector<std::uint16_t>(
                {100, 101, 200, 201, 0, 0}),
            "首个兼容帧缺行应补零");

    require(!assembler.ingest(packetView(geometry, frame11row3)),
            "第二个兼容帧不应提前发布");
    auto second = assembler.ingest(packetView(geometry, frame12row1));
    require(second.has_value() && second->frameNumber == 11,
            "第二个兼容帧未在帧切换时发布");
    require(second->missingRows == 1 &&
                second->rowsFilledFromPreviousFrame == 1,
            "第二个兼容帧未记录沿用上一帧行");
    require(second->pixels == std::vector<std::uint16_t>(
                {110, 111, 200, 201, 310, 311}),
            "缺失行没有沿用上一帧对应行");

    const auto stats = assembler.stats();
    require(stats.completedFrames == 2 && stats.partialFrames == 2,
            "兼容帧发布统计错误");
    require(stats.missingRowsPublished == 2 &&
                stats.rowsFilledFromPreviousFrame == 1,
            "兼容帧补行累计统计错误");

    FrameGeometry strictGeometry{2, 3, 16, 2, 3};
    FrameAssembler strictAssembler(strictGeometry);
    require(!strictAssembler.ingest(packetView(strictGeometry, frame10row1)),
            "严格模式一行数据不应完成帧");
    require(!strictAssembler.ingest(packetView(strictGeometry, frame10row2)),
            "严格模式两行数据不应完成帧");
    require(!strictAssembler.ingest(packetView(strictGeometry, frame11row1)),
            "严格模式不得发布不完整帧");
}

void testFrameNumberWrap()
{
    const FrameGeometry geometry{2, 1, 16, 2, 3};
    FrameAssembler assembler(geometry);
    auto last = makePacket(geometry, 65535, 1, 10);
    auto wrapped = makePacket(geometry, 0, 1, 20);
    auto old = makePacket(geometry, 65534, 1, 30);
    require(assembler.ingest(packetView(geometry, last)).has_value(),
            "回绕前帧未完成");
    require(assembler.ingest(packetView(geometry, wrapped)).has_value(),
            "帧号65535到0没有识别为更新");
    require(!assembler.ingest(packetView(geometry, old)),
            "回绕后的旧帧不应发布");
    require(assembler.stats().expiredRows == 1, "回绕旧帧统计错误");
}

} // namespace

int main()
{
    try {
        testGeometryValidation();
        testEverySplitPosition();
        testNoiseAndInvalidHeaderRecovery();
        testFalseMagicInsidePixelPayload();
        testAssemblerOutOfOrderAndDuplicate();
        testSharedAssemblerFromFourEndpointThreads();
        testFrameWindowAndLateRows();
        testWindowsCompatiblePartialFramePublishing();
        testFrameNumberWrap();
        std::cout << "USB协议层回归测试全部通过。" << std::endl;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "USB协议层回归测试失败：" << error.what() << std::endl;
        return 1;
    }
}
