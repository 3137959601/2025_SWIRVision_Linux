#include "transfer_thread.h"

#include "widget_image.h"

#include <QDebug>
#include <QMutexLocker>

#include <cstring>

void transferThread::setFrameSpec(int w, int h, int headerBytes,
                                  int bytesPerPixel)
{
    frameWidth = w;
    frameHeight = h;
    frameHeader = headerBytes;
    pixelBytes = bytesPerPixel;

    swir::usb::FrameGeometry geometry;
    if (w > 0)
        geometry.width = std::size_t(w);
    if (h > 0)
        geometry.height = std::size_t(h);
    if (headerBytes > 0)
        geometry.headerBytes = std::size_t(headerBytes);
    if (bytesPerPixel > 0)
        geometry.bytesPerPixel = std::size_t(bytesPerPixel);
    geometry.frameWindow = 3;
    // 400W模组现场连续帧稳定只到2040/2048行。原Windows上位机会在帧号
    // 切换时直接显示旧帧；Linux项目仅对此规格允许最多8行兼容补帧。
    geometry.allowedMissingRows = (w == 2048 && h == 2048) ? 8 : 0;

    rowStreamParser.configure(geometry);
    if (!frameAssembler)
        frameAssembler = std::make_shared<swir::usb::FrameAssembler>();
    frameAssembler->configure(geometry);
    emit specChanged(frameWidth, frameHeight, pixelBytes);
}

void transferThread::processReceivedBytes(const std::uint8_t *data,
                                          std::size_t size)
{
    if (!frameAssembler || !data || size == 0)
        return;

    rowStreamParser.consume(data, size, [this](const swir::usb::RowPacketView &row) {
        #ifdef Q_OS_LINUX
        if (linuxHeaderSamplesLogged < 12) {
            qInfo() << "Linux USB行头样本，端点："
                    << QStringLiteral("0x%1").arg(usbPipeID, 2, 16,
                                                    QLatin1Char('0'))
                    << "样本序号：" << linuxHeaderSamplesLogged
                    << "帧号：" << row.frameNumber
                    << "行号：" << row.rowNumber;
            ++linuxHeaderSamplesLogged;
        }
        #endif
        auto completed = frameAssembler->ingest(row);
        if (!completed)
            return;

        if (completed->missingRows != 0) {
            ++partialFrameWarnings;
            if (partialFrameWarnings <= 5 ||
                partialFrameWarnings % 100 == 0) {
                qWarning() << "USB兼容发布不完整帧，帧号："
                           << completed->frameNumber
                           << "缺失行：" << completed->missingRows
                           << "沿用上一帧行："
                           << completed->rowsFilledFromPreviousFrame
                           << "本线程兼容发布次数："
                           << partialFrameWarnings;
            }
        }

        bool copied = false;
        {
            QMutexLocker imageLock(&widget_image::s_imgMutex);
            std::uint16_t *destination = widget_image::rawPtr();
            const int destinationStride = widget_image::rawStridePx();
            const std::size_t expectedPixels =
                std::size_t(frameWidth) * std::size_t(frameHeight);
            if (destination && destinationStride >= frameWidth &&
                completed->pixels.size() == expectedPixels) {
                for (int y = 0; y < frameHeight; ++y) {
                    const std::uint16_t *source = completed->pixels.data() +
                                                  std::size_t(y) * frameWidth;
                    std::memcpy(destination + std::size_t(y) * destinationStride,
                                source,
                                std::size_t(frameWidth) * sizeof(std::uint16_t));
                }
                copied = true;
            }
        }
        if (copied)
            emit updatapic();
    });
}
