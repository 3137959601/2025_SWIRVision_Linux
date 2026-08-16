#include "tih_usb_device.h"

tihUSBDevice::tihUSBDevice(QString devPath)
    : devicePath(std::move(devPath))
{
}

tihUSBDevice::~tihUSBDevice()
{
    close();
}

bool tihUSBDevice::isOpen()
{
    return opened;
}

bool tihUSBDevice::open()
{
    qWarning() << "Linux USB 后端尚未接入 libusb，设备打开被明确拒绝："
               << devicePath;
    opened = false;
    return false;
}

void tihUSBDevice::close()
{
    opened = false;
    epList.clear();
}

bool tihUSBDevice::reboot()
{
    return false;
}

QList<UsbEndpointInfo> tihUSBDevice::endPoints() const
{
    return epList;
}
