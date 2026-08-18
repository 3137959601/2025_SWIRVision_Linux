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
