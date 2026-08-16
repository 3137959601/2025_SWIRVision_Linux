#include "tih_usb_device.h"
#include <QDebug>

tihUSBDevice::tihUSBDevice(QString devPath)
{
    devicePath = devPath;

    usbHandle = INVALID_HANDLE_VALUE;
    deviceHandle = INVALID_HANDLE_VALUE;
}

tihUSBDevice::~tihUSBDevice()
{
    devicePath.clear();
    close();
}

bool tihUSBDevice::isOpen()
{
    if (usbHandle == INVALID_HANDLE_VALUE) {
        return false;
    } else {
        return true;
    }
}

bool tihUSBDevice::open()
{
    bool ret = false;
    TCHAR path[MAX_PATH] = {0};

    if (deviceHandle != INVALID_HANDLE_VALUE) {
        return ret;
    }

    if (0 == devicePath.toWCharArray(path)) {
        return ret;
    }
    deviceHandle = CreateFile(path,
                              GENERIC_WRITE | GENERIC_READ,
                              FILE_SHARE_WRITE | FILE_SHARE_READ,
                              NULL,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                              NULL);
    if (deviceHandle == INVALID_HANDLE_VALUE)
        return ret;

    ret = WinUsb_Initialize(deviceHandle, &usbHandle);
    if (!ret) {
        CloseHandle(deviceHandle);
        return ret;
    }

    analyzeDescriptor();
    return ret;
}

void tihUSBDevice::close()
{
    if (usbHandle != INVALID_HANDLE_VALUE) {
        WinUsb_Free(usbHandle);
        usbHandle = INVALID_HANDLE_VALUE;
    }

    if (deviceHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(deviceHandle);
        deviceHandle = INVALID_HANDLE_VALUE;
    }
}

QList<UsbEndpointInfo> tihUSBDevice::endPoints() const
{
    return epList;
}

HANDLE tihUSBDevice::registerIsoBuffer(UCHAR id, UCHAR *buf, ULONG len)
{
    HANDLE dev = INVALID_HANDLE_VALUE;

    if (WinUsb_RegisterIsochBuffer(usbHandle, id, buf, len, &dev))
        return dev;
    else
        return INVALID_HANDLE_VALUE;
}

void tihUSBDevice::unRegisterIsoBuffer(HANDLE dev)
{
    WinUsb_UnregisterIsochBuffer(dev);
}

bool tihUSBDevice::reqIsoWrite(HANDLE dev, ULONG len, LPOVERLAPPED ov)
{
    return WinUsb_WriteIsochPipeAsap(dev, 0, len, FALSE, ov);
}

bool tihUSBDevice::reqIsoRead(HANDLE dev,
                              ULONG len,
                              ULONG packs,
                              PUSBD_ISO_PACKET_DESCRIPTOR sta,
                              LPOVERLAPPED ov)
{
    return WinUsb_ReadIsochPipeAsap(dev, 0, len, FALSE, packs, sta, ov);
}

void tihUSBDevice::flush(UCHAR id)
{
    WinUsb_FlushPipe(usbHandle, id);
}

bool tihUSBDevice::reqXfer(UCHAR id, UCHAR *buf, ULONG len, LPOVERLAPPED ov)
{
    if (id & 0x80) {
        return WinUsb_ReadPipe(usbHandle, id, buf, len, NULL, ov);
    } else {
        return WinUsb_WritePipe(usbHandle, id, buf, len, NULL, ov);
    }
}

bool tihUSBDevice::checkXferOver(ULONG *transfered, LPOVERLAPPED ov)
{
    *transfered = 0;
    return WinUsb_GetOverlappedResult(usbHandle, ov, transfered, FALSE);
}

bool tihUSBDevice::reboot()
{
    OVERLAPPED ov;
    WINUSB_SETUP_PACKET pkt;
    ULONG transfered;

    memset(&ov, 0, sizeof(OVERLAPPED));

    pkt.RequestType = 0x42;
    pkt.Request = 0x54;
    pkt.Value = 0;
    pkt.Length = 0;
    pkt.Index = 0;

    WinUsb_ControlTransfer(usbHandle, pkt, NULL, 0, NULL, &ov);

    return WinUsb_GetOverlappedResult(usbHandle, &ov, &transfered, TRUE);
}
