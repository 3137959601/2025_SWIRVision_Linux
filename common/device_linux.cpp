#include "device.h"

#include <QDebug>

#include <libusb.h>

QStringList RetrieveDevice(uint16_t vid, uint16_t pid)
{
    QStringList result;
    libusb_context *context = nullptr;
    const int initResult = libusb_init(&context);
    if (initResult != LIBUSB_SUCCESS) {
        qWarning() << "libusb初始化失败：" << libusb_error_name(initResult);
        return result;
    }

    libusb_device **devices = nullptr;
    const ssize_t count = libusb_get_device_list(context, &devices);
    if (count < 0) {
        qWarning() << "libusb设备枚举失败：" << libusb_error_name(int(count));
        libusb_exit(context);
        return result;
    }

    for (ssize_t index = 0; index < count; ++index) {
        libusb_device_descriptor descriptor{};
        if (libusb_get_device_descriptor(devices[index], &descriptor) !=
            LIBUSB_SUCCESS) {
            continue;
        }
        if (descriptor.idVendor != vid || descriptor.idProduct != pid)
            continue;

        result.append(QStringLiteral("libusb://%1/%2?vid=%3&pid=%4")
                          .arg(libusb_get_bus_number(devices[index]), 3, 10,
                               QLatin1Char('0'))
                          .arg(libusb_get_device_address(devices[index]), 3, 10,
                               QLatin1Char('0'))
                          .arg(descriptor.idVendor, 4, 16, QLatin1Char('0'))
                          .arg(descriptor.idProduct, 4, 16, QLatin1Char('0')));
    }

    libusb_free_device_list(devices, 1);
    libusb_exit(context);
    return result;
}
