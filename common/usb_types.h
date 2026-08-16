#ifndef USB_TYPES_H
#define USB_TYPES_H

#include <QtGlobal>

enum class UsbPipeType
{
    Control,
    Isochronous,
    Bulk,
    Interrupt,
    Unknown
};

struct UsbEndpointInfo
{
    UsbPipeType pipeType = UsbPipeType::Unknown;
    quint8 address = 0;
    quint16 maximumPacketSize = 0;
    quint8 interval = 0;
    quint32 maximumBytesPerInterval = 0;
};

#endif // USB_TYPES_H
