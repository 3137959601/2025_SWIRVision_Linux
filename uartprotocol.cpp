#include "uartprotocol.h"

#include <QtGlobal>
#include <cmath>

namespace {

quint8 byteAt(const QByteArray &data, int index)
{
    return static_cast<quint8>(data.at(index));
}

quint16 u16(const QByteArray &data, int index)
{
    return static_cast<quint16>((quint16(byteAt(data, index)) << 8) |
                                byteAt(data, index + 1));
}

} // namespace

QByteArray UartProtocol::makeCommand(quint8 code, quint8 control, quint16 value)
{
    QByteArray frame(8, char(0));
    frame[0] = char(0xEA);
    frame[1] = char(0x01);
    frame[2] = char(code);
    frame[3] = char(control);
    frame[4] = char((value >> 8) & 0xFF);
    frame[5] = char(value & 0xFF);
    frame[6] = char(quint8(code + control + quint8(frame[4]) + quint8(frame[5])));
    frame[7] = char(0x0A);
    return frame;
}

QByteArray UartProtocol::makeCrosshairCommand(quint16 x, quint16 y)
{
    x &= 0x0FFF;
    y &= 0x0FFF;
    QByteArray frame(8, char(0));
    frame[0] = char(0xEA);
    frame[1] = char(0x01);
    frame[2] = char(0x24);
    frame[3] = char((x >> 4) & 0xFF);
    frame[4] = char(((x & 0x0F) << 4) | ((y >> 8) & 0x0F));
    frame[5] = char(y & 0xFF);
    frame[6] = char(quint8(quint8(frame[2]) + quint8(frame[3]) +
                           quint8(frame[4]) + quint8(frame[5])));
    frame[7] = char(0x0A);
    return frame;
}

QList<TelemetryFrame> UartProtocol::parseTelemetry(QByteArray &buffer, int *checksumErrors)
{
    QList<TelemetryFrame> frames;
    int errors = 0;

    while (!buffer.isEmpty()) {
        const int header = buffer.indexOf(char(0xEA));
        if (header < 0) {
            buffer.clear();
            break;
        }
        if (header > 0)
            buffer.remove(0, header);
        if (buffer.size() < TelemetryFrameSize)
            break;

        if (byteAt(buffer, 1) != 0x01 || byteAt(buffer, 2) != 0xAA ||
            byteAt(buffer, 3) != 0xFF || byteAt(buffer, 39) != 0x0A) {
            buffer.remove(0, 1);
            continue;
        }

        quint8 sum = 0;
        for (int i = 2; i <= 37; ++i)
            sum = quint8(sum + byteAt(buffer, i));
        if (sum != byteAt(buffer, 38)) {
            ++errors;
            buffer.remove(0, 1);
            continue;
        }

        TelemetryFrame frame;
        frame.timestamp = QDateTime::currentDateTime();
        frame.intTime = u16(buffer, 4);
        frame.boardTempRaw = u16(buffer, 6);
        frame.itecRaw = u16(buffer, 8);
        frame.vtecRaw = u16(buffer, 10);
        frame.tmpActualRaw = u16(buffer, 12);
        frame.tmpSetRaw = u16(buffer, 14);
        frame.sharpness = u16(buffer, 16);
        frame.crosshairX = u16(buffer, 18) & 0x0FFF;
        frame.crosshairY = u16(buffer, 20) & 0x0FFF;
        frame.comSetRaw = u16(buffer, 22) & 0x0FFF;
        frame.frameMetric = u16(buffer, 24);
        frame.linearKLevel = byteAt(buffer, 26) & 0x3F;
        frame.linearBLevel = byteAt(buffer, 27) & 0x3F;
        // byte28低4位为手动区域，高2位为DS18B20实际温度组：0/1/2=-10/15/40C。
        frame.tpRegion = byteAt(buffer, 28) & 0x0F;
        frame.temperatureGroup = (byteAt(buffer, 28) >> 4) & 0x03;
        frame.autoRegion = byteAt(buffer, 29) & 0x0F;
        frame.status0 = byteAt(buffer, 30);
        frame.status1 = byteAt(buffer, 31);
        frame.version0 = byteAt(buffer, 32);
        frame.version1 = byteAt(buffer, 33);
        frame.versionYear = byteAt(buffer, 34);
        frame.versionMonth = byteAt(buffer, 35);
        frame.versionDay = byteAt(buffer, 36);
        frame.flashStatus = byteAt(buffer, 37);
        frames.append(frame);
        buffer.remove(0, TelemetryFrameSize);
    }

    if (checksumErrors)
        *checksumErrors = errors;
    return frames;
}

double UartProtocol::ds18b20Temperature(quint16 raw)
{
    const bool negative = (raw & 0x8000) != 0;
    double value = double(raw & 0x07FF) * 0.0625;
    return negative ? -value : value;
}

double UartProtocol::comDacCodeToVoltage(quint16 code)
{
    return double(code & 0x0FFF) * 2.5 / 4095.0;
}

quint16 UartProtocol::comVoltageToDacCode(double voltage)
{
    return quint16(qRound(qBound(0.0, voltage, 2.5) * 4095.0 / 2.5));
}

double UartProtocol::tecVoltageToTemperature(double voltage)
{
    return 23.5 * voltage * voltage * voltage
         - 98.5 * voltage * voltage
         + 170.0 * voltage - 79.7;
}

double UartProtocol::tecTemperatureToVoltage(double temperature)
{
    double lo = 0.3;
    double hi = 2.39;
    const double target = qBound(tecVoltageToTemperature(lo), temperature,
                                 tecVoltageToTemperature(hi));
    for (int i = 0; i < 40; ++i) {
        const double mid = (lo + hi) * 0.5;
        if (tecVoltageToTemperature(mid) < target)
            lo = mid;
        else
            hi = mid;
    }
    return (lo + hi) * 0.5;
}

quint16 UartProtocol::tecVoltageToDacCode(double voltage)
{
    const int code = qRound(qBound(0.3, voltage, 2.39) * 65535.0 / 2.5);
    return quint16(qBound(0, code, 65535));
}
