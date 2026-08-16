#ifndef DEVICE_H
#define DEVICE_H

#include <QString>
#include <QStringList>
#include <cstdint>

QStringList RetrieveDevice(uint16_t vid, uint16_t pid);

#endif // DEVICE_H
