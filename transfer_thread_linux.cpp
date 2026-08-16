#include "transfer_thread.h"

#include <QDebug>
#include <QMutexLocker>

QMutex transferThread::s_streamMutex;
QFile transferThread::s_streamFile;
bool transferThread::s_streamEnabled = false;

transferThread::transferThread(tihUSBDevice *dev, bool check, bool mode)
    : usbInterface(dev), dataCheckEnable(check), highSpeedModeEn(mode)
{
}

transferThread::~transferThread()
{
    stop();
    wait();
    delete[] transBuf;
}

void transferThread::setUsbPipe(uint8_t id, UsbPipeType type)
{
    usbPipeID = id;
    usbPipeType = type;
}

void transferThread::setDataPattern(uint32_t type, uint32_t size)
{
    transferDataType = type;
    transferPackSize = size;
}

void transferThread::setIsoInfo(uint32_t nbytes, uint32_t interval)
{
    isoNbytes = nbytes;
    isoInterval = interval;
}

void transferThread::stop()
{
    stopFlag = true;
}

void transferThread::startStreamSave(const QString &fileName)
{
    QMutexLocker locker(&s_streamMutex);
    if (s_streamFile.isOpen())
        s_streamFile.close();
    s_streamFile.setFileName(fileName);
    s_streamEnabled = s_streamFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
}

void transferThread::stopStreamSave()
{
    QMutexLocker locker(&s_streamMutex);
    s_streamEnabled = false;
    if (s_streamFile.isOpen())
        s_streamFile.close();
}

void transferThread::run()
{
    qWarning() << "Linux USB 传输线程未启动：libusb 后端尚未实现。";
}

void transferThread::isoTransfer()
{
}

void transferThread::bulkTransfer()
{
}

void transferThread::dataCheck(char *, uint32_t)
{
}
