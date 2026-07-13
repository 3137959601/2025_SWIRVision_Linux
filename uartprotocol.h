#ifndef UARTPROTOCOL_H
#define UARTPROTOCOL_H

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMetaType>

struct TelemetryFrame
{
    QDateTime timestamp;
    quint16 intTime = 0;
    quint16 boardTempRaw = 0;
    quint16 itecRaw = 0;
    quint16 vtecRaw = 0;
    quint16 tmpActualRaw = 0;
    quint16 tmpSetRaw = 0;
    quint16 sharpness = 0;
    quint16 crosshairX = 0;
    quint16 crosshairY = 0;
    quint16 comSetRaw = 0;
    quint16 frameMetric = 0;
    quint8 linearKLevel = 0;
    quint8 linearBLevel = 0;
    quint8 tpRegion = 0;
    quint8 temperatureGroup = 0;
    quint8 autoRegion = 0;
    quint8 status0 = 0;
    quint8 status1 = 0;
    quint8 version0 = 0;
    quint8 version1 = 0;
    quint8 versionYear = 0;
    quint8 versionMonth = 0;
    quint8 versionDay = 0;
    quint8 flashStatus = 0;

    bool autoExposure() const { return status0 & 0x80; }
    bool tecEnabled() const { return status0 & 0x40; }
    bool tecPowerEnabled() const { return status0 & 0x20; }
    bool twoPointEnabled() const { return status0 & 0x10; }
    bool badPixelEnabled() const { return status0 & 0x08; }
    bool medianEnabled() const { return status0 & 0x04; }
    bool histogramEnabled() const { return status0 & 0x02; }
    bool flipEnabled() const { return status0 & 0x01; }
    bool stretchEnabled() const { return status1 & 0x80; }
    bool crosshairEnabled() const { return status1 & 0x40; }
    bool sharpnessEnabled() const { return status1 & 0x20; }
    bool gainEnabled() const { return status1 & 0x10; }
    bool transferEnabled() const { return status1 & 0x08; }
    bool autoRoiEnabled() const { return status1 & 0x04; }
    bool iffEnabled() const { return status1 & 0x02; }
    bool autoTemperatureEnabled() const { return status1 & 0x01; }
    bool flashInitDone() const { return flashStatus & 0x04; }
    bool configSaveDone() const { return flashStatus & 0x02; }
    bool twoPointSaveDone() const { return flashStatus & 0x01; }
};

Q_DECLARE_METATYPE(TelemetryFrame)
Q_DECLARE_METATYPE(QList<TelemetryFrame>)

namespace UartProtocol {

constexpr int TelemetryFrameSize = 40;

QByteArray makeCommand(quint8 code, quint8 control, quint16 value = 0);
QByteArray makeCrosshairCommand(quint16 x, quint16 y);
QList<TelemetryFrame> parseTelemetry(QByteArray &buffer, int *checksumErrors = nullptr);

double ds18b20Temperature(quint16 raw);
double comDacCodeToVoltage(quint16 code);
quint16 comVoltageToDacCode(double voltage);
double tecVoltageToTemperature(double voltage);
double tecTemperatureToVoltage(double temperature);
quint16 tecVoltageToDacCode(double voltage);

} // namespace UartProtocol

#endif // UARTPROTOCOL_H
