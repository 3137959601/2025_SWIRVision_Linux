#include "serialworker.h"

#include <QCoreApplication>
#include <QTimer>

#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <pty.h>
#include <unistd.h>

namespace {

QByteArray makeTelemetryFrame()
{
    QByteArray frame(UartProtocol::TelemetryFrameSize, char(0));
    frame[0] = char(0xea);
    frame[1] = char(0x01);
    frame[2] = char(0xaa);
    frame[3] = char(0xff);
    frame[4] = char(0x12);
    frame[5] = char(0x34);
    frame[6] = char(0x01);
    frame[7] = char(0x90);
    frame[8] = char(0x03);
    frame[9] = char(0xe8);
    frame[10] = char(0x07);
    frame[11] = char(0xd0);
    frame[16] = char(0x00);
    frame[17] = char(0x2a);
    frame[30] = char(0xff);
    frame[31] = char(0xff);
    frame[37] = char(0x07);
    quint8 checksum = 0;
    for (int index = 2; index <= 37; ++index)
        checksum = quint8(checksum + quint8(frame.at(index)));
    frame[38] = char(checksum);
    frame[39] = char(0x0a);
    return frame;
}

bool readExactWithTimeout(int descriptor, QByteArray &output, int expected,
                          int timeoutMs)
{
    output.clear();
    while (output.size() < expected) {
        pollfd pollDescriptor{descriptor, POLLIN, 0};
        const int pollResult = ::poll(&pollDescriptor, 1, timeoutMs);
        if (pollResult <= 0)
            return false;
        std::array<char, 256> buffer{};
        const ssize_t count = ::read(descriptor, buffer.data(), buffer.size());
        if (count <= 0)
            return false;
        output.append(buffer.data(), int(count));
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    int master = -1;
    int slave = -1;
    std::array<char, 256> slaveName{};
    if (::openpty(&master, &slave, slaveName.data(), nullptr, nullptr) != 0) {
        std::cerr << "创建伪终端失败：" << std::strerror(errno) << std::endl;
        return 1;
    }
    ::close(slave);

    SerialWorker worker;
    worker.SerialPortInit(QString::fromLocal8Bit(slaveName.data()));
    if (!worker.serialWorker || !worker.serialWorker->isOpen()) {
        std::cerr << "Qt SerialPort无法打开伪终端：" << slaveName.data()
                  << std::endl;
        ::close(master);
        return 2;
    }

    const QByteArray outbound = QByteArray::fromHex("EA0120FF00001F0A");
    worker.SerialSendBytes_Slot(outbound);
    if (!worker.serialWorker->waitForBytesWritten(1000)) {
        std::cerr << "Qt SerialPort写等待超时："
                  << worker.serialWorker->errorString().toStdString() << std::endl;
        ::close(master);
        return 3;
    }
    QByteArray observed;
    if (!readExactWithTimeout(master, observed, outbound.size(), 1000) ||
        observed != outbound) {
        std::cerr << "伪终端未收到预期串口字节" << std::endl;
        ::close(master);
        return 4;
    }

    bool telemetryReceived = false;
    QObject::connect(&worker, &SerialWorker::telemetryFramesReady, &application,
                     [&](const QList<TelemetryFrame> &frames, int errors) {
        if (errors == 0 && frames.size() == 1 &&
            frames.front().intTime == 0x1234 &&
            frames.front().sharpness == 42) {
            telemetryReceived = true;
            application.quit();
        }
    });

    const QByteArray telemetry = makeTelemetryFrame();
    if (::write(master, telemetry.constData(), std::size_t(telemetry.size())) !=
        telemetry.size()) {
        std::cerr << "写入伪终端遥测帧失败" << std::endl;
        ::close(master);
        return 5;
    }
    QTimer::singleShot(2000, &application, &QCoreApplication::quit);
    application.exec();

    worker.SerialClose();
    ::close(master);
    if (!telemetryReceived) {
        std::cerr << "Qt SerialPort未在超时前解析遥测帧" << std::endl;
        return 6;
    }

    std::cout << "Linux伪终端串口收发与遥测解析测试全部通过。" << std::endl;
    return 0;
}
