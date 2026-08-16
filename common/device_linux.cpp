#include "device.h"

QStringList RetrieveDevice(uint16_t, uint16_t)
{
    // Linux USB 枚举将在 libusb 后端接入后实现。当前明确返回空列表，
    // 仅用于打通不依赖真机的 GUI、图像处理和离线回放链路。
    return {};
}
