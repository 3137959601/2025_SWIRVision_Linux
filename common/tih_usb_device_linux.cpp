#include "tih_usb_device.h"

#include <QRegularExpression>

#include <libusb.h>

#include <atomic>
#include <thread>

struct LinuxUsbState
{
    libusb_context *context = nullptr;
    libusb_device_handle *handle = nullptr;
    int interfaceNumber = -1;
    bool interfaceClaimed = false;
    std::atomic_bool eventLoopRunning{false};
    std::thread eventThread;
};

namespace {

UsbPipeType convertTransferType(std::uint8_t attributes)
{
    switch (attributes & LIBUSB_TRANSFER_TYPE_MASK) {
    case LIBUSB_TRANSFER_TYPE_CONTROL: return UsbPipeType::Control;
    case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS: return UsbPipeType::Isochronous;
    case LIBUSB_TRANSFER_TYPE_BULK: return UsbPipeType::Bulk;
    case LIBUSB_TRANSFER_TYPE_INTERRUPT: return UsbPipeType::Interrupt;
    default: return UsbPipeType::Unknown;
    }
}

} // namespace

tihUSBDevice::tihUSBDevice(QString devPath)
    : linuxState(std::make_unique<LinuxUsbState>()),
      devicePath(std::move(devPath))
{
}

tihUSBDevice::~tihUSBDevice()
{
    close();
}

bool tihUSBDevice::isOpen()
{
    return opened && linuxState && linuxState->handle;
}

bool tihUSBDevice::open()
{
    if (isOpen()) {
        errorString = QStringLiteral("设备已经打开");
        return false;
    }
    close();
    linuxState = std::make_unique<LinuxUsbState>();
    epList.clear();
    errorString.clear();

    static const QRegularExpression pathPattern(
        QStringLiteral("^libusb://(\\d+)/(\\d+)\\?vid=([0-9a-fA-F]{4})&pid=([0-9a-fA-F]{4})$"));
    const auto match = pathPattern.match(devicePath);
    if (!match.hasMatch()) {
        errorString = QStringLiteral("无法解析libusb设备路径：%1").arg(devicePath);
        return false;
    }
    bool busOk = false;
    bool addressOk = false;
    bool vidOk = false;
    bool pidOk = false;
    const int bus = match.captured(1).toInt(&busOk);
    const int address = match.captured(2).toInt(&addressOk);
    const auto expectedVid = std::uint16_t(match.captured(3).toUInt(&vidOk, 16));
    const auto expectedPid = std::uint16_t(match.captured(4).toUInt(&pidOk, 16));
    if (!busOk || !addressOk || bus < 0 || bus > 255 ||
        address < 0 || address > 255 || !vidOk || !pidOk) {
        errorString = QStringLiteral("libusb总线号或设备地址无效：%1")
                          .arg(devicePath);
        return false;
    }

    int result = libusb_init(&linuxState->context);
    if (result != LIBUSB_SUCCESS) {
        errorString = QStringLiteral("libusb初始化失败：%1")
                          .arg(QString::fromLatin1(libusb_error_name(result)));
        return false;
    }

    libusb_device **devices = nullptr;
    const ssize_t count = libusb_get_device_list(linuxState->context, &devices);
    if (count < 0) {
        errorString = QStringLiteral("libusb设备列表读取失败：%1")
                          .arg(QString::fromLatin1(libusb_error_name(int(count))));
        close();
        return false;
    }

    libusb_device *selected = nullptr;
    for (ssize_t index = 0; index < count; ++index) {
        libusb_device_descriptor descriptor{};
        if (libusb_get_device_descriptor(devices[index], &descriptor) !=
            LIBUSB_SUCCESS) {
            continue;
        }
        if (libusb_get_bus_number(devices[index]) == bus &&
            libusb_get_device_address(devices[index]) == address &&
            descriptor.idVendor == expectedVid &&
            descriptor.idProduct == expectedPid) {
            selected = devices[index];
            break;
        }
    }
    if (!selected) {
        errorString = QStringLiteral("设备已离线或地址已变化，请重新枚举：%1")
                          .arg(devicePath);
        libusb_free_device_list(devices, 1);
        close();
        return false;
    }

    result = libusb_open(selected, &linuxState->handle);
    if (result != LIBUSB_SUCCESS) {
        errorString = QStringLiteral("libusb打开设备失败：%1；请检查USB权限")
                          .arg(QString::fromLatin1(libusb_error_name(result)));
        libusb_free_device_list(devices, 1);
        close();
        return false;
    }

    libusb_config_descriptor *configuration = nullptr;
    result = libusb_get_active_config_descriptor(selected, &configuration);
    if (result != LIBUSB_SUCCESS || !configuration ||
        configuration->bNumInterfaces == 0) {
        errorString = QStringLiteral("无法读取USB活动配置描述符：%1")
                          .arg(QString::fromLatin1(libusb_error_name(result)));
        libusb_free_device_list(devices, 1);
        close();
        return false;
    }

    const libusb_interface &interface = configuration->interface[0];
    if (interface.num_altsetting == 0) {
        errorString = QStringLiteral("USB接口0没有可用的备用设置");
        libusb_free_config_descriptor(configuration);
        libusb_free_device_list(devices, 1);
        close();
        return false;
    }
    const libusb_interface_descriptor &alternate = interface.altsetting[0];
    linuxState->interfaceNumber = alternate.bInterfaceNumber;

    libusb_set_auto_detach_kernel_driver(linuxState->handle, 1);
    result = libusb_claim_interface(linuxState->handle,
                                    linuxState->interfaceNumber);
    if (result != LIBUSB_SUCCESS) {
        errorString = QStringLiteral("USB接口%1声明失败：%2；请检查权限或内核驱动占用")
                          .arg(linuxState->interfaceNumber)
                          .arg(QString::fromLatin1(libusb_error_name(result)));
        libusb_free_config_descriptor(configuration);
        libusb_free_device_list(devices, 1);
        close();
        return false;
    }
    linuxState->interfaceClaimed = true;

    for (int index = 0; index < alternate.bNumEndpoints; ++index) {
        const libusb_endpoint_descriptor &endpoint = alternate.endpoint[index];
        UsbEndpointInfo info;
        info.pipeType = convertTransferType(endpoint.bmAttributes);
        info.address = endpoint.bEndpointAddress;
        info.maximumPacketSize = endpoint.wMaxPacketSize;
        info.interval = endpoint.bInterval;
        info.maximumBytesPerInterval = endpoint.wMaxPacketSize;

        libusb_ss_endpoint_companion_descriptor *companion = nullptr;
        if (libusb_get_ss_endpoint_companion_descriptor(
                linuxState->context, &endpoint, &companion) == LIBUSB_SUCCESS &&
            companion) {
            info.maximumBytesPerInterval = companion->wBytesPerInterval;
            libusb_free_ss_endpoint_companion_descriptor(companion);
        }
        epList.append(info);
    }

    libusb_free_config_descriptor(configuration);
    libusb_free_device_list(devices, 1);

    linuxState->eventLoopRunning = true;
    LinuxUsbState *state = linuxState.get();
    linuxState->eventThread = std::thread([state] {
        while (state->eventLoopRunning.load()) {
            timeval timeout{0, 100000};
            libusb_handle_events_timeout_completed(state->context, &timeout,
                                                   nullptr);
        }
    });
    opened = true;
    return true;
}

void tihUSBDevice::close()
{
    opened = false;
    epList.clear();
    if (!linuxState)
        return;

    linuxState->eventLoopRunning = false;
    if (linuxState->context)
        libusb_interrupt_event_handler(linuxState->context);
    if (linuxState->eventThread.joinable())
        linuxState->eventThread.join();

    if (linuxState->handle) {
        if (linuxState->interfaceClaimed && linuxState->interfaceNumber >= 0) {
            libusb_release_interface(linuxState->handle,
                                     linuxState->interfaceNumber);
        }
        libusb_close(linuxState->handle);
        linuxState->handle = nullptr;
    }
    if (linuxState->context) {
        libusb_exit(linuxState->context);
        linuxState->context = nullptr;
    }
}

bool tihUSBDevice::reboot()
{
    if (!isOpen()) {
        errorString = QStringLiteral("设备未打开，不能发送重启控制请求");
        return false;
    }
    const int result = libusb_control_transfer(
        linuxState->handle,
        std::uint8_t(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_ENDPOINT),
        0x54, 0, 0, nullptr, 0, 1000);
    if (result < 0) {
        errorString = QStringLiteral("设备重启控制请求失败：%1")
                          .arg(QString::fromLatin1(libusb_error_name(result)));
        return false;
    }
    return true;
}

QList<UsbEndpointInfo> tihUSBDevice::endPoints() const
{
    return epList;
}

libusb_device_handle *tihUSBDevice::nativeHandle() const
{
    return linuxState ? linuxState->handle : nullptr;
}
