#ifndef WINUSB_SUPP_H
#define WINUSB_SUPP_H

#include <stdint.h>

/* Table 9-5. Descriptor Types */
#define B_DESCTYPE_DEVICE                       1
#define B_DESCTYPE_CONFIGURATION                2
#define B_DESCTYPE_STRING                       3
#define B_DESCTYPE_INTERFACE                    4
#define B_DESCTYPE_ENDPOINT                     5
#define B_DESCTYPE_DEVICE_QUALIFIER             6
#define B_DESCTYPE_OTHER_SPEED_CONFIGURATION    7
#define B_DESCTYPE_INTERFACE_POWER              8
#define B_DESCTYPE_INTERFACE_ASSOCIATION        11
#define B_DESCTYPE_BOS                          15
#define B_DESCTYPE_DEVICE_CAPABILITY            16
#define B_DESCTYPE_SS_USB_ENDPOINT_COMPANION    48
#define B_DESCTYPE_PIPE_USAGE                   36

/* Endpoint transfer type */
#define EP_TYPE_CONTROL                 0
#define EP_TYPE_ISOCHRONOUS             1
#define EP_TYPE_BULK                    2
#define EP_TYPE_INTERRUPT               3

#pragma pack(1)
/* All standard descriptors have these 2 fields at the beginning */
typedef struct usbDescriptorHeader {
    uint8_t bLength;
    uint8_t bDescriptorType;
}usbDescHdr_t;

typedef struct deviceDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendorID;
    uint16_t idProductID;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNum;
    uint8_t  bNumConfigurations;
}devDesc_t;

typedef struct configurationDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
}cfgDesc_t;

typedef struct interfaceDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlterSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
}ifsDesc_t;

typedef struct endpointDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
}epDesc_t;

typedef struct ss_endpoint_companion {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bMaxBurst;
    uint8_t  bmAttributes;
    uint16_t wBytesPerInterval;
}ssepDesc_t;

typedef struct bosDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumDeviceCaps;
}bosDesc_t;

#pragma pack()

#endif // WINUSB_SUPP_H
