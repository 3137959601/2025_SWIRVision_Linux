#include "../uartprotocol.h"

#include <QCoreApplication>
#include <QDebug>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) qFatal("FAIL: %s", message);
}

QByteArray sampleFrame()
{
    return QByteArray::fromHex(
        "EA01AAFF03E801E904FC051C058D058C116207FF07FF0C2890000A0A0001B7B4000000000004890A");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QByteArray sample = sampleFrame();
    require(sample.size() == UartProtocol::TelemetryFrameSize, "sample length");

    QByteArray buffer = sample;
    int errors = 0;
    QList<TelemetryFrame> frames = UartProtocol::parseTelemetry(buffer, &errors);
    require(frames.size() == 1 && errors == 0 && buffer.isEmpty(), "complete frame");
    require(frames[0].intTime == 1000 && frames[0].sharpness == 0x1162, "core fields");
    require(frames[0].crosshairX == 2047 && frames[0].crosshairY == 2047, "coordinates");
    require(frames[0].comSetRaw == 0x0C28 && frames[0].frameMetric == 0x9000, "settings");
    require(frames[0].status0 == 0xB7 && frames[0].status1 == 0xB4, "status fields");
    require(frames[0].tpRegion == 0 && frames[0].temperatureGroup == 0 &&
            frames[0].autoRegion == 1, "region fields");
    require(frames[0].autoRoiEnabled(), "auto ROI status");
    require(frames[0].flashStatus == 0x04, "flash status");

    QByteArray highGroup = sample;
    highGroup[28] = char(0x20); // 温度组2，手动区域0
    highGroup[38] = char(quint8(highGroup[38]) + 0x20);
    buffer = highGroup;
    frames = UartProtocol::parseTelemetry(buffer, &errors);
    require(frames.size() == 1 && frames[0].temperatureGroup == 2,
            "temperature group field");

    buffer = sample.left(17);
    frames = UartProtocol::parseTelemetry(buffer, &errors);
    require(frames.isEmpty() && buffer.size() == 17, "partial frame retained");
    buffer.append(sample.mid(17));
    frames = UartProtocol::parseTelemetry(buffer, &errors);
    require(frames.size() == 1 && buffer.isEmpty(), "split frame completed");

    buffer = QByteArray::fromHex("001122") + sample + sample;
    frames = UartProtocol::parseTelemetry(buffer, &errors);
    require(frames.size() == 2 && buffer.isEmpty(), "noise and sticky frames");

    QByteArray bad = sample;
    bad[38] = char(quint8(bad[38]) + 1);
    buffer = bad + sample;
    frames = UartProtocol::parseTelemetry(buffer, &errors);
    require(errors == 1 && frames.size() == 1 && buffer.isEmpty(), "checksum recovery");

    require(UartProtocol::makeCommand(0x20, 0xFF).toHex().toUpper() == "EA0120FF00001F0A",
            "command checksum");
    require(UartProtocol::makeCrosshairCommand(0x100, 0x140).toHex().toUpper() ==
            "EA0124100140750A", "crosshair command");
    require(UartProtocol::makeCommand(0x28, 0xFF).toHex().toUpper() == "EA0128FF0000270A",
            "center ROI command");
    require(UartProtocol::makeCommand(0x28, 0xF0).toHex().toUpper() == "EA0128F00000180A",
            "full-frame command");
    require(UartProtocol::makeCommand(0x36, 0xFF).toHex().toUpper() == "EA0136FF0000350A",
            "auto temperature enable command");
    require(UartProtocol::makeCommand(0x36, 0xF0).toHex().toUpper() == "EA0136F00000260A",
            "auto temperature disable command");
    require(UartProtocol::comVoltageToDacCode(2.5) == 0x0FFF, "COM full scale");
    require(qAbs(UartProtocol::comDacCodeToVoltage(0x0C28) - 1.9) < 0.001,
            "COM voltage conversion");

    qInfo() << "PASS uartprotocol_test";
    return 0;
}
