#ifndef DEVICE_H
#define DEVICE_H

#include <windows.h>
#include <QString>
#include <QStringList>

QStringList RetrieveDevice(uint16_t vid, uint16_t pid);

#endif // DEVICE_H
