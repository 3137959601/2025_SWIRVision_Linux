#ifndef TIH_USB_DEVICE_H
#define TIH_USB_DEVICE_H

#include <windows.h>
#include <winusb.h>
#include <winusbio.h>
#include <QList>
#include <QDebug>

class tihUSBDevice
{
public:
    tihUSBDevice(QString devPath);
    ~tihUSBDevice();

    bool isOpen();
    bool open();
    void close();

    bool reqXfer(UCHAR id, UCHAR *buf, ULONG len, LPOVERLAPPED ov);
    bool checkXferOver(ULONG *transfered, LPOVERLAPPED ov);
    bool reboot();
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

    QList<WINUSB_PIPE_INFORMATION_EX> endPoints();
private:
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

            epList.append(pipe);
        }
    }

    WINUSB_INTERFACE_HANDLE usbHandle;
    HANDLE deviceHandle;
    QString devicePath;

    QList<WINUSB_PIPE_INFORMATION_EX> epList;
};

#endif // TIH_USB_DEVICE_H
