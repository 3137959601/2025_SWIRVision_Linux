#include "transfer_thread.h"

#include <QDebug>
#include <QMutexLocker>

#include <libusb.h>

#include <atomic>
#include <cstring>
#include <new>

extern std::atomic_uint64_t g_transOk;
extern std::atomic_uint64_t g_transErr;

namespace {

constexpr std::size_t kLinuxRequestQueue = 64;

const char *transferStatusName(libusb_transfer_status status)
{
    switch (status) {
    case LIBUSB_TRANSFER_COMPLETED: return "completed";
    case LIBUSB_TRANSFER_ERROR: return "error";
    case LIBUSB_TRANSFER_TIMED_OUT: return "timed_out";
    case LIBUSB_TRANSFER_CANCELLED: return "cancelled";
    case LIBUSB_TRANSFER_STALL: return "stall";
    case LIBUSB_TRANSFER_NO_DEVICE: return "no_device";
    case LIBUSB_TRANSFER_OVERFLOW: return "overflow";
    }
    return "unknown";
}

} // namespace

QMutex transferThread::s_streamMutex;
QFile transferThread::s_streamFile;
bool transferThread::s_streamEnabled = false;

transferThread::transferThread(
    tihUSBDevice *dev, bool check, bool mode,
    std::shared_ptr<swir::usb::FrameAssembler> sharedFrameAssembler)
    : usbInterface(dev), dataCheckEnable(check), highSpeedModeEn(mode),
      frameAssembler(std::move(sharedFrameAssembler))
{
}

transferThread::~transferThread()
{
    stop();
    wait();
    delete[] transBuf;
}

void transferThread::setUsbPipe(uint8_t id, UsbPipeType type)
{
    usbPipeID = id;
    usbPipeType = type;
}

void transferThread::setDataPattern(uint32_t type, uint32_t size)
{
    transferDataType = type;
    transferPackSize = size;
}

void transferThread::setIsoInfo(uint32_t nbytes, uint32_t interval)
{
    isoNbytes = nbytes;
    isoInterval = interval;
}

void transferThread::stop()
{
    stopFlag = true;
    std::vector<libusb_transfer *> transfers;
    {
        std::lock_guard<std::mutex> lock(linuxTransferMutex);
        transfers = linuxTransfers;
    }
    for (libusb_transfer *transfer : transfers) {
        if (transfer)
            libusb_cancel_transfer(transfer);
    }
}

void transferThread::startStreamSave(const QString &fileName)
{
    QMutexLocker locker(&s_streamMutex);
    if (s_streamFile.isOpen())
        s_streamFile.close();
    s_streamFile.setFileName(fileName);
    s_streamEnabled = s_streamFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
}

void transferThread::stopStreamSave()
{
    QMutexLocker locker(&s_streamMutex);
    s_streamEnabled = false;
    if (s_streamFile.isOpen())
        s_streamFile.close();
}

void transferThread::run()
{
    if (!usbInterface || !usbInterface->isOpen() ||
        !usbInterface->nativeHandle()) {
        qWarning() << "Linux USB传输未启动：设备没有通过libusb打开。";
        return;
    }
    if (!(usbPipeID & LIBUSB_ENDPOINT_IN) ||
        usbPipeType != UsbPipeType::Bulk) {
        qWarning() << "Linux USB传输只接受Bulk IN端点："
                   << QStringLiteral("0x%1").arg(usbPipeID, 2, 16,
                                                   QLatin1Char('0'));
        return;
    }
    if (transferPackSize == 0) {
        qWarning() << "Linux USB传输块大小为0，拒绝启动。";
        return;
    }

    stopFlag = false;
    rowStreamParser.reset();
    transBuf = new (std::nothrow)
        unsigned char[std::size_t(transferPackSize) * kLinuxRequestQueue]();
    if (!transBuf) {
        qWarning() << "Linux USB传输缓冲分配失败："
                   << transferPackSize << "x" << kLinuxRequestQueue;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(linuxTransferMutex);
        linuxTransfers.clear();
        linuxTransfers.reserve(kLinuxRequestQueue);
        linuxActiveTransfers = 0;
    }

    for (std::size_t index = 0; index < kLinuxRequestQueue; ++index) {
        if (stopFlag.load())
            break;
        libusb_transfer *transfer = libusb_alloc_transfer(0);
        if (!transfer) {
            qWarning() << "libusb传输对象分配失败，索引：" << index;
            break;
        }
        unsigned char *buffer = transBuf + index * transferPackSize;
        libusb_fill_bulk_transfer(transfer, usbInterface->nativeHandle(),
                                  usbPipeID, buffer, int(transferPackSize),
                                  &transferThread::linuxTransferCallback,
                                  this, 0);
        {
            std::lock_guard<std::mutex> lock(linuxTransferMutex);
            linuxTransfers.push_back(transfer);
            ++linuxActiveTransfers;
        }
        const int submitResult = libusb_submit_transfer(transfer);
        if (submitResult != LIBUSB_SUCCESS) {
            qWarning() << "libusb异步Bulk IN提交失败，索引：" << index
                       << "原因：" << libusb_error_name(submitResult);
            {
                std::lock_guard<std::mutex> lock(linuxTransferMutex);
                linuxTransfers.pop_back();
                --linuxActiveTransfers;
            }
            libusb_free_transfer(transfer);
            break;
        }
    }

    {
        std::unique_lock<std::mutex> lock(linuxTransferMutex);
        if (linuxActiveTransfers == 0) {
            qWarning() << "没有成功提交任何libusb异步Bulk IN请求。";
        } else {
            linuxTransferFinished.wait(lock, [this] {
                return linuxActiveTransfers == 0;
            });
        }
        for (libusb_transfer *transfer : linuxTransfers)
            libusb_free_transfer(transfer);
        linuxTransfers.clear();
    }

    delete[] transBuf;
    transBuf = nullptr;
}

void transferThread::isoTransfer()
{
}

void transferThread::bulkTransfer()
{
}

void transferThread::dataCheck(char *, uint32_t)
{
}

void transferThread::linuxTransferCallback(libusb_transfer *transfer)
{
    auto *owner = static_cast<transferThread *>(transfer->user_data);
    if (!owner)
        return;

    bool shouldResubmit = !owner->stopFlag.load();
    if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
        if (transfer->actual_length > 0) {
            owner->processReceivedBytes(transfer->buffer,
                                        std::size_t(transfer->actual_length));
            g_transOk.fetch_add(std::uint64_t(transfer->actual_length));
            if (transfer->actual_length < transfer->length) {
                g_transErr.fetch_add(
                    std::uint64_t(transfer->length - transfer->actual_length));
            }
        }
    } else if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
        shouldResubmit = false;
    } else if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE) {
        shouldResubmit = false;
        owner->stopFlag = true;
        g_transErr.fetch_add(std::uint64_t(transfer->length));
        qWarning() << "libusb设备已断开，端点："
                   << QStringLiteral("0x%1").arg(owner->usbPipeID, 2, 16,
                                                   QLatin1Char('0'));
    } else {
        g_transErr.fetch_add(std::uint64_t(transfer->length));
        qWarning() << "libusb Bulk IN完成异常，端点："
                   << QStringLiteral("0x%1").arg(owner->usbPipeID, 2, 16,
                                                   QLatin1Char('0'))
                   << "状态：" << transferStatusName(transfer->status);
    }

    if (shouldResubmit) {
        const int result = libusb_submit_transfer(transfer);
        if (result == LIBUSB_SUCCESS)
            return;
        g_transErr.fetch_add(std::uint64_t(transfer->length));
        qWarning() << "libusb Bulk IN重新提交失败："
                   << libusb_error_name(result);
    }

    {
        std::lock_guard<std::mutex> lock(owner->linuxTransferMutex);
        if (owner->linuxActiveTransfers > 0)
            --owner->linuxActiveTransfers;
    }
    owner->linuxTransferFinished.notify_all();
}
