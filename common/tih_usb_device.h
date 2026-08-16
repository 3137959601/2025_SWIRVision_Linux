#ifndef TIH_USB_DEVICE_H
#define TIH_USB_DEVICE_H

#include <QList>
#include <QDebug>
#include "usb_types.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <winusb.h>
#include <winusbio.h>
#endif

class tihUSBDevice
{
public:
    tihUSBDevice(QString devPath);
    ~tihUSBDevice();

    bool isOpen();
    bool open();
    void close();

    bool reboot();

    QList<UsbEndpointInfo> endPoints() const;

#ifdef Q_OS_WIN
    bool reqXfer(UCHAR id, UCHAR *buf, ULONG len, LPOVERLAPPED ov);
    bool checkXferOver(ULONG *transfered, LPOVERLAPPED ov);
    void flush(UCHAR id);
    /* only for ISO */
    HANDLE registerIsoBuffer(UCHAR id, UCHAR *buf, ULONG len);
    void unRegisterIsoBuffer(HANDLE dev);
    bool reqIsoWrite(HANDLE dev, ULONG len, LPOVERLAPPED ov);
    bool reqIsoRead(HANDLE dev,
                    ULONG len,
                    ULONG packs,
                    PUSBD_ISO_PACKET_DESCRIPTOR sta,
                    LPOVERLAPPED ov);
#endif
private:
#ifdef Q_OS_WIN
    void analyzeDescriptor()
    {
        USB_INTERFACE_DESCRIPTOR usbIf;
        WINUSB_PIPE_INFORMATION_EX pipe;
        int i;

        if (!WinUsb_QueryInterfaceSettings(usbHandle, 0, &usbIf))
            return;

        for (i = 0; i < usbIf.bNumEndpoints; i++) {
            if (!WinUsb_QueryPipeEx(usbHandle, 0, (UCHAR)i, &pipe))
                return;

            UsbEndpointInfo info;
            switch (pipe.PipeType) {
            case UsbdPipeTypeControl: info.pipeType = UsbPipeType::Control; break;
            case UsbdPipeTypeIsochronous: info.pipeType = UsbPipeType::Isochronous; break;
            case UsbdPipeTypeBulk: info.pipeType = UsbPipeType::Bulk; break;
            case UsbdPipeTypeInterrupt: info.pipeType = UsbPipeType::Interrupt; break;
            default: info.pipeType = UsbPipeType::Unknown; break;
            }
            info.address = pipe.PipeId;
            info.maximumPacketSize = pipe.MaximumPacketSize;
            info.interval = pipe.Interval;
            info.maximumBytesPerInterval = pipe.MaximumBytesPerInterval;
            epList.append(info);
        }
    }

    WINUSB_INTERFACE_HANDLE usbHandle;
    HANDLE deviceHandle;
#else
    bool opened = false;
#endif
    QString devicePath;

    QList<UsbEndpointInfo> epList;
};

#endif // TIH_USB_DEVICE_H
