#include "usb_frame_pipeline.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace swir::usb {
namespace {

constexpr std::uint8_t kMagic[] = {0x90, 0xeb, 0x00, 0x00};

std::uint16_t readLittleEndian16(const std::uint8_t *data) noexcept
{
    return std::uint16_t(data[0]) |
           std::uint16_t(std::uint16_t(data[1]) << 8);
}

bool sameGeometry(const FrameGeometry &lhs, const FrameGeometry &rhs) noexcept
{
    return lhs.width == rhs.width && lhs.height == rhs.height &&
           lhs.headerBytes == rhs.headerBytes &&
           lhs.bytesPerPixel == rhs.bytesPerPixel &&
           lhs.frameWindow == rhs.frameWindow;
}

} // namespace

bool FrameGeometry::isValid() const noexcept
{
    if (width == 0 || height == 0 || headerBytes < 12 ||
        bytesPerPixel != 2 || frameWindow == 0) {
        return false;
    }
    if (width > std::numeric_limits<std::size_t>::max() / bytesPerPixel) {
        return false;
    }
    const std::size_t payload = width * bytesPerPixel;
    return headerBytes <= std::numeric_limits<std::size_t>::max() - payload;
}

std::size_t FrameGeometry::payloadBytes() const noexcept
{
    return isValid() ? width * bytesPerPixel : 0;
}

std::size_t FrameGeometry::packetBytes() const noexcept
{
    return isValid() ? headerBytes + payloadBytes() : 0;
}

RowStreamParser::RowStreamParser(FrameGeometry geometry)
{
    configure(geometry);
}

bool RowStreamParser::configure(FrameGeometry geometry)
{
    if (!geometry.isValid()) {
        m_geometry = {};
        reset();
        return false;
    }
    m_geometry = geometry;
    reset();
    m_carry.reserve(m_geometry.packetBytes());
    return true;
}

void RowStreamParser::reset()
{
    m_stats = {};
    m_carry.clear();
}

bool RowStreamParser::consume(const std::uint8_t *data, std::size_t size,
                              const PacketHandler &handler)
{
    if (!m_geometry.isValid() || (!data && size != 0) || !handler) {
        return false;
    }

    m_stats.inputBytes += size;
    std::size_t offset = 0;
    const std::size_t packetSize = m_geometry.packetBytes();

    // m_carry只保存跨块的不完整包，或最多3字节的魔数前缀。
    // 正常完整包直接引用USB完成缓冲，避免复制整个传输块。
    while (!m_carry.empty() && offset < size) {
        const std::size_t required = packetSize > m_carry.size()
                                         ? packetSize - m_carry.size()
                                         : std::size_t(1);
        const std::size_t take = std::min(required, size - offset);
        m_carry.insert(m_carry.end(), data + offset, data + offset + take);
        offset += take;

        std::vector<std::uint8_t> joined;
        joined.swap(m_carry);
        consumeContiguous(joined.data(), joined.size(), handler);
    }

    if (offset < size) {
        consumeContiguous(data + offset, size - offset, handler);
    }
    return true;
}

void RowStreamParser::consumeContiguous(const std::uint8_t *data,
                                        std::size_t size,
                                        const PacketHandler &handler)
{
    const std::size_t packetSize = m_geometry.packetBytes();
    const std::size_t payloadSize = m_geometry.payloadBytes();
    std::size_t offset = 0;

    while (offset < size) {
        const std::size_t magicAt = findMagic(data, size, offset);
        if (magicAt == size) {
            const std::size_t keep = magicPrefixSuffixLength(data + offset,
                                                              size - offset);
            m_stats.discardedBytes += size - offset - keep;
            if (keep != 0) {
                m_carry.assign(data + size - keep, data + size);
            }
            return;
        }

        m_stats.discardedBytes += magicAt - offset;
        if (size - magicAt < packetSize) {
            m_carry.assign(data + magicAt, data + size);
            return;
        }

        const std::uint16_t rowNumber = readLittleEndian16(data + magicAt + 8);
        if (rowNumber == 0 || rowNumber > m_geometry.height) {
            ++m_stats.invalidHeaders;
            ++m_stats.discardedBytes;
            offset = magicAt + 1;
            continue;
        }

        RowPacketView packet;
        packet.rowNumber = rowNumber;
        packet.frameNumber = readLittleEndian16(data + magicAt + 10);
        packet.payload = data + magicAt + m_geometry.headerBytes;
        packet.payloadSize = payloadSize;
        handler(packet);
        ++m_stats.packets;
        offset = magicAt + packetSize;
    }
}

std::size_t RowStreamParser::findMagic(const std::uint8_t *data,
                                       std::size_t size,
                                       std::size_t start) noexcept
{
    if (!data || start >= size || size - start < sizeof(kMagic)) {
        return size;
    }
    for (std::size_t i = start; i + sizeof(kMagic) <= size; ++i) {
        if (std::memcmp(data + i, kMagic, sizeof(kMagic)) == 0) {
            return i;
        }
    }
    return size;
}

std::size_t RowStreamParser::magicPrefixSuffixLength(
    const std::uint8_t *data, std::size_t size) noexcept
{
    const std::size_t limit = std::min(size, sizeof(kMagic) - 1);
    for (std::size_t length = limit; length != 0; --length) {
        if (std::memcmp(data + size - length, kMagic, length) == 0) {
            return length;
        }
    }
    return 0;
}

FrameAssembler::FrameAssembler(FrameGeometry geometry)
{
    configure(geometry);
}

bool FrameAssembler::configure(FrameGeometry geometry)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!geometry.isValid()) {
        m_geometry = {};
        resetLocked();
        return false;
    }
    if (!sameGeometry(m_geometry, geometry)) {
        m_geometry = geometry;
        resetLocked();
    }
    return true;
}

void FrameAssembler::reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    resetLocked();
}

void FrameAssembler::resetLocked()
{
    m_slots.clear();
    m_hasNewest = false;
    m_newest = 0;
    m_hasPublished = false;
    m_lastPublished = 0;
    m_stats = {};
}

std::optional<CompletedFrame> FrameAssembler::ingest(
    const RowPacketView &packet)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::size_t expectedPayload = m_geometry.payloadBytes();
    if (!m_geometry.isValid() || !packet.payload ||
        packet.payloadSize != expectedPayload || packet.rowNumber == 0 ||
        packet.rowNumber > m_geometry.height) {
        ++m_stats.invalidRows;
        return std::nullopt;
    }

    if (m_hasPublished &&
        (packet.frameNumber == m_lastPublished ||
         !isNewer(packet.frameNumber, m_lastPublished))) {
        ++m_stats.expiredRows;
        return std::nullopt;
    }

    if (!m_hasNewest || isNewer(packet.frameNumber, m_newest)) {
        m_newest = packet.frameNumber;
        m_hasNewest = true;
    }

    const auto beforePrune = m_slots.size();
    m_slots.erase(std::remove_if(m_slots.begin(), m_slots.end(),
                                 [this](const Slot &slot) {
        const std::uint16_t age = std::uint16_t(m_newest - slot.frameNumber);
        return age >= m_geometry.frameWindow && age < 0x8000;
    }), m_slots.end());
    m_stats.evictedFrames += beforePrune - m_slots.size();

    const std::uint16_t age = std::uint16_t(m_newest - packet.frameNumber);
    if (age >= m_geometry.frameWindow) {
        ++m_stats.expiredRows;
        return std::nullopt;
    }

    auto slot = std::find_if(m_slots.begin(), m_slots.end(),
                             [&packet](const Slot &candidate) {
        return candidate.frameNumber == packet.frameNumber;
    });
    if (slot == m_slots.end()) {
        Slot newSlot;
        newSlot.frameNumber = packet.frameNumber;
        newSlot.pixels.resize(m_geometry.width * m_geometry.height);
        newSlot.rowReceived.assign(m_geometry.height, std::uint8_t(0));
        m_slots.push_back(std::move(newSlot));
        slot = std::prev(m_slots.end());
    }

    const std::size_t rowIndex = packet.rowNumber - 1;
    std::uint16_t *destination = slot->pixels.data() + rowIndex * m_geometry.width;
    const std::uint16_t endianProbe = 1;
    const bool hostIsLittleEndian =
        *reinterpret_cast<const std::uint8_t *>(&endianProbe) == 1;
    if (hostIsLittleEndian) {
        std::memcpy(destination, packet.payload, expectedPayload);
    } else {
        for (std::size_t x = 0; x < m_geometry.width; ++x) {
            destination[x] = readLittleEndian16(packet.payload + x * 2);
        }
    }

    if (slot->rowReceived[rowIndex]) {
        ++m_stats.duplicateRows;
        return std::nullopt;
    }
    slot->rowReceived[rowIndex] = 1;
    ++slot->rowCount;
    ++m_stats.acceptedRows;

    if (slot->rowCount != m_geometry.height) {
        return std::nullopt;
    }

    CompletedFrame completed;
    completed.frameNumber = packet.frameNumber;
    completed.pixels = std::move(slot->pixels);
    m_lastPublished = packet.frameNumber;
    m_hasPublished = true;
    ++m_stats.completedFrames;

    m_slots.erase(std::remove_if(m_slots.begin(), m_slots.end(),
                                 [this](const Slot &candidate) {
        return candidate.frameNumber == m_lastPublished ||
               !isNewer(candidate.frameNumber, m_lastPublished);
    }), m_slots.end());
    return completed;
}

FrameGeometry FrameAssembler::geometry() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_geometry;
}

FrameAssemblerStats FrameAssembler::stats() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    FrameAssemblerStats snapshot = m_stats;
    snapshot.activeFrames = m_slots.size();
    snapshot.newestFrameNumber = m_hasNewest ? m_newest : 0;
    for (const Slot &slot : m_slots)
        snapshot.fullestFrameRows = std::max(snapshot.fullestFrameRows,
                                             slot.rowCount);
    return snapshot;
}

bool FrameAssembler::isNewer(std::uint16_t candidate,
                             std::uint16_t current) noexcept
{
    const std::uint16_t delta = std::uint16_t(candidate - current);
    return delta != 0 && delta < 0x8000;
}

} // namespace swir::usb
