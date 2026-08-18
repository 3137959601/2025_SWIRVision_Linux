#ifndef USB_FRAME_PIPELINE_H
#define USB_FRAME_PIPELINE_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace swir::usb {

struct FrameGeometry
{
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t headerBytes = 16;
    std::size_t bytesPerPixel = 2;
    std::size_t frameWindow = 3;

    bool isValid() const noexcept;
    std::size_t payloadBytes() const noexcept;
    std::size_t packetBytes() const noexcept;
};

struct RowPacketView
{
    std::uint16_t frameNumber = 0;
    std::uint16_t rowNumber = 0;
    const std::uint8_t *payload = nullptr;
    std::size_t payloadSize = 0;
};

struct StreamParserStats
{
    std::uint64_t inputBytes = 0;
    std::uint64_t packets = 0;
    std::uint64_t discardedBytes = 0;
    std::uint64_t invalidHeaders = 0;
};

class RowStreamParser
{
public:
    using PacketHandler = std::function<void(const RowPacketView &)>;

    explicit RowStreamParser(FrameGeometry geometry = {});

    bool configure(FrameGeometry geometry);
    void reset();
    bool consume(const std::uint8_t *data, std::size_t size,
                 const PacketHandler &handler);

    const FrameGeometry &geometry() const noexcept { return m_geometry; }
    const StreamParserStats &stats() const noexcept { return m_stats; }
    std::size_t bufferedBytes() const noexcept { return m_carry.size(); }

private:
    void consumeContiguous(const std::uint8_t *data, std::size_t size,
                           const PacketHandler &handler);
    static std::size_t findMagic(const std::uint8_t *data, std::size_t size,
                                 std::size_t start) noexcept;
    static std::size_t magicPrefixSuffixLength(const std::uint8_t *data,
                                               std::size_t size) noexcept;

    FrameGeometry m_geometry;
    StreamParserStats m_stats;
    std::vector<std::uint8_t> m_carry;
};

struct CompletedFrame
{
    std::uint16_t frameNumber = 0;
    std::vector<std::uint16_t> pixels;
};

struct FrameAssemblerStats
{
    std::uint64_t acceptedRows = 0;
    std::uint64_t duplicateRows = 0;
    std::uint64_t invalidRows = 0;
    std::uint64_t expiredRows = 0;
    std::uint64_t evictedFrames = 0;
    std::uint64_t completedFrames = 0;
    std::size_t activeFrames = 0;
    std::size_t fullestFrameRows = 0;
    std::uint16_t newestFrameNumber = 0;
};

class FrameAssembler
{
public:
    explicit FrameAssembler(FrameGeometry geometry = {});

    bool configure(FrameGeometry geometry);
    void reset();
    std::optional<CompletedFrame> ingest(const RowPacketView &packet);

    FrameGeometry geometry() const;
    FrameAssemblerStats stats() const;

private:
    struct Slot
    {
        std::uint16_t frameNumber = 0;
        std::vector<std::uint16_t> pixels;
        std::vector<std::uint8_t> rowReceived;
        std::size_t rowCount = 0;
    };

    static bool isNewer(std::uint16_t candidate,
                        std::uint16_t current) noexcept;
    void resetLocked();

    mutable std::mutex m_mutex;
    FrameGeometry m_geometry;
    std::deque<Slot> m_slots;
    bool m_hasNewest = false;
    std::uint16_t m_newest = 0;
    bool m_hasPublished = false;
    std::uint16_t m_lastPublished = 0;
    FrameAssemblerStats m_stats;
};

} // namespace swir::usb

#endif // USB_FRAME_PIPELINE_H
