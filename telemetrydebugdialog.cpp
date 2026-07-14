#include "telemetrydebugdialog.h"

#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStringList>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

QString hex16(quint16 value) { return QStringLiteral("0x%1").arg(value, 4, 16, QLatin1Char('0')).toUpper(); }

QString statusMark(const QString &name, bool enabled, bool available = true)
{
    const QString color = available
        ? (enabled ? QStringLiteral("#16803c") : QStringLiteral("#c23838"))
        : QStringLiteral("#7a7a7a");
    const QString state = available ? (enabled ? QStringLiteral("开") : QStringLiteral("关"))
                                    : QStringLiteral("未接入");
    return QStringLiteral("<span style='color:%1; font-size:18px; font-weight:700'>●</span> %2:%3")
        .arg(color, name, state);
}

QString completionMark(const QString &name, bool complete)
{
    const QString color = complete ? QStringLiteral("#16803c") : QStringLiteral("#c23838");
    return QStringLiteral("<span style='color:%1; font-size:18px; font-weight:700'>●</span> %2:%3")
        .arg(color, name, complete ? QStringLiteral("完成") : QStringLiteral("未完成"));
}

QString tecMonitorText(quint16 rawMillivolts, bool powerEnabled)
{
    if (!powerEnabled)
        return QStringLiteral("--（TEC电源关闭）");
    const double voltage = rawMillivolts / 1000.0;
    if (voltage < 0.3 || voltage > 2.39)
        return QStringLiteral("无效监测值 %1 V (raw=%2)")
            .arg(voltage, 0, 'f', 3).arg(rawMillivolts);
    return QStringLiteral("%1 ℃ (%2 V)")
        .arg(UartProtocol::tecVoltageToTemperature(voltage), 0, 'f', 2)
        .arg(voltage, 0, 'f', 3);
}

} // namespace

TelemetryDebugDialog::TelemetryDebugDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("SWIR_400W串口调试状态"));
    resize(980, 820);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(buildStatusPanel());
    layout->addWidget(buildCommandPanel(), 1);
}

QWidget *TelemetryDebugDialog::buildStatusPanel()
{
    auto *page = new QGroupBox(QStringLiteral("实时状态"), this);
    auto *layout = new QVBoxLayout(page);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(5);

    auto addField = [this, page, grid](int row, int column, const QString &key,
                                       const QString &title, int valueSpan = 1) {
        auto *titleLabel = new QLabel(title, page);
        auto *valueLabel = new QLabel(QStringLiteral("--"), page);
        valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        valueLabel->setWordWrap(true);
        m_labels.insert(key, valueLabel);
        grid->addWidget(titleLabel, row, column * 2);
        grid->addWidget(valueLabel, row, column * 2 + 1, 1, valueSpan);
    };

    addField(0, 0, "time", QStringLiteral("更新时间"));
    addField(0, 1, "int", QStringLiteral("当前生效积分时间"));
    addField(1, 0, "metric", QStringLiteral("像素均值"));
    addField(1, 1, "sharp", QStringLiteral("锐度"));
    addField(2, 0, "board", QStringLiteral("板间温度"));
    addField(2, 1, "com", QStringLiteral("COM设定"));
    addField(3, 0, "itec", QStringLiteral("ITEC"));
    addField(3, 1, "vtec", QStringLiteral("VTEC"));
    addField(4, 0, "actual", QStringLiteral("TEC实际温度"));
    addField(4, 1, "set", QStringLiteral("TEC设定温度"));
    addField(5, 0, "cross", QStringLiteral("十字线坐标"));
    addField(5, 1, "kb", QStringLiteral("K/B档位"));
    addField(6, 0, "tp_region", QStringLiteral("手动两点区域"));
    addField(6, 1, "auto_region", QStringLiteral("当前生效区域"));
    addField(7, 0, "temp_group", QStringLiteral("实际温度档位"));
    addField(7, 1, "version", QStringLiteral("版本/日期"));
    addField(8, 0, "algo0", QStringLiteral("算法状态0"), 3);
    addField(9, 0, "algo1", QStringLiteral("算法状态1"), 3);
    addField(10, 0, "flash", QStringLiteral("Flash状态"), 3);
    layout->addLayout(grid);

    auto *save = new QGridLayout;
    auto *choose = new QPushButton(QStringLiteral("选择CSV"), page);
    m_csvButton = new QPushButton(QStringLiteral("开始记录"), page);
    m_csvLabel = new QLabel(QStringLiteral("未选择文件"), page);
    m_errorLabel = new QLabel(QStringLiteral("校验错误：0"), page);
    save->addWidget(choose, 0, 0);
    save->addWidget(m_csvButton, 0, 1);
    save->addWidget(m_errorLabel, 0, 2);
    save->addWidget(m_csvLabel, 1, 0, 1, 3);
    layout->addLayout(save);
    connect(choose, &QPushButton::clicked, this, &TelemetryDebugDialog::chooseCsvFile);
    connect(m_csvButton, &QPushButton::clicked, this, &TelemetryDebugDialog::toggleCsv);
    return page;
}

QWidget *TelemetryDebugDialog::buildCommandPanel()
{
    auto *scroll = new QScrollArea(this);
    auto *panel = new QWidget(scroll);
    auto *layout = new QGridLayout(panel);
    int row = 0;

    auto title = [&](const QString &text) {
        auto *label = new QLabel(QStringLiteral("<b>%1</b>").arg(text), panel);
        layout->addWidget(label, row++, 0, 1, 6);
    };
    auto button = [&](int col, const QString &text, quint8 code, quint8 control,
                      quint16 value = 0, bool enabled = true) {
        auto *b = new QPushButton(text, panel);
        connect(b, &QPushButton::clicked, this, [=]() { emitCommand(code, control, value); });
        b->setEnabled(enabled);
        if (!enabled)
            b->setToolTip(QStringLiteral("当前FPGA图像链路未接入此功能"));
        layout->addWidget(b, row, col);
    };
    auto finishRow = [&]() { ++row; };

    title(QStringLiteral("核心设置"));
    layout->addWidget(new QLabel(QStringLiteral("积分(ms)"), panel), row, 0);
    m_intTime = new QDoubleSpinBox(panel);
    m_intTime->setRange(0, 655.35); m_intTime->setDecimals(2); m_intTime->setValue(10.0);
    layout->addWidget(m_intTime, row, 1);
    auto *intSet = new QPushButton(QStringLiteral("设置积分"), panel);
    connect(intSet, &QPushButton::clicked, this, [this]() {
        emitCommand(0x05, 0xFF, quint16(qBound(0, qRound(m_intTime->value() * 100.0), 65535)));
    });
    layout->addWidget(intSet, row, 2);
    finishRow();

    auto addAutoRangeRow = [&](const QString &name, quint8 command,
                               double defaultMin, double defaultMax) {
        layout->addWidget(new QLabel(name, panel), row, 0);
        auto *autoMin = new QDoubleSpinBox(panel);
        auto *autoMax = new QDoubleSpinBox(panel);
        autoMin->setRange(0.01, 40.95); autoMin->setDecimals(2); autoMin->setValue(defaultMin);
        autoMax->setRange(0.01, 40.95); autoMax->setDecimals(2); autoMax->setValue(defaultMax);
        layout->addWidget(autoMin, row, 1);
        layout->addWidget(autoMax, row, 2);
        auto *setAutoRange = new QPushButton(QStringLiteral("设置范围"), panel);
        connect(setAutoRange, &QPushButton::clicked, this, [this, autoMin, autoMax, command]() {
            const quint16 minRaw = quint16(qRound(autoMin->value() * 100.0));
            const quint16 maxRaw = quint16(qRound(autoMax->value() * 100.0));
            if (minRaw > maxRaw || minRaw > 0x0FFF || maxRaw > 0x0FFF)
                return;
            const quint32 payload = (quint32(minRaw) << 12) | maxRaw;
            emitCommand(command, quint8(payload >> 16), quint16(payload));
        });
        layout->addWidget(setAutoRange, row, 3);
        finishRow();
    };
    addAutoRangeRow(QStringLiteral("低温范围(ms)"), 0x32, 1.00, 30.00);
    addAutoRangeRow(QStringLiteral("中温范围(ms)"), 0x33, 2.00, 20.00);
    addAutoRangeRow(QStringLiteral("高温范围(ms)"), 0x34, 0.30, 3.00);

    layout->addWidget(new QLabel(QStringLiteral("亮度阈值"), panel), row, 0);
    auto *thrInc = new QSpinBox(panel);
    auto *thrDec = new QSpinBox(panel);
    thrInc->setRange(0, 8190); thrInc->setSingleStep(2); thrInc->setValue(3000);
    thrDec->setRange(0, 8190); thrDec->setSingleStep(2); thrDec->setValue(6500);
    layout->addWidget(thrInc, row, 1);
    layout->addWidget(thrDec, row, 2);
    auto *setThresholds = new QPushButton(QStringLiteral("设置阈值"), panel);
    connect(setThresholds, &QPushButton::clicked, this, [this, thrInc, thrDec]() {
        if (thrInc->value() >= thrDec->value()) return;
        const quint32 payload = (quint32(thrInc->value() >> 1) << 12)
                              | quint32(thrDec->value() >> 1);
        emitCommand(0x35, quint8(payload >> 16), quint16(payload));
    });
    layout->addWidget(setThresholds, row, 3);
    finishRow();

    layout->addWidget(new QLabel(QStringLiteral("COM电压(V)"), panel), row, 0);
    m_comVoltage = new QDoubleSpinBox(panel);
    m_comVoltage->setRange(0.0, 2.5);
    m_comVoltage->setDecimals(3);
    m_comVoltage->setSingleStep(0.01);
    m_comVoltage->setValue(UartProtocol::comDacCodeToVoltage(0x0B84));
    layout->addWidget(m_comVoltage, row, 1);
    auto *comSet = new QPushButton(QStringLiteral("设置COM"), panel);
    connect(comSet, &QPushButton::clicked, this, [this]() {
        emitCommand(0x06, 0xFF, UartProtocol::comVoltageToDacCode(m_comVoltage->value()));
    });
    layout->addWidget(comSet, row, 2);
    finishRow();

    layout->addWidget(new QLabel(QStringLiteral("TEC温度(℃)"), panel), row, 0);
    m_tecTemp = new QDoubleSpinBox(panel); m_tecTemp->setRange(-20.0, 60.0); m_tecTemp->setValue(15.0);
    layout->addWidget(m_tecTemp, row, 1);
    auto *tecSet = new QPushButton(QStringLiteral("设置TEC"), panel);
    connect(tecSet, &QPushButton::clicked, this, [this]() {
        const double voltage = UartProtocol::tecTemperatureToVoltage(m_tecTemp->value());
        emitCommand(0x0F, 0xFF, UartProtocol::tecVoltageToDacCode(voltage));
    });
    layout->addWidget(tecSet, row, 2);
    button(3, QStringLiteral("TEC使能开"), 0x1F, 0xFF);
    button(4, QStringLiteral("TEC使能关"), 0x1F, 0xF0); finishRow();
    button(0, QStringLiteral("TEC电源开"), 0x1F, 0xAF);
    button(1, QStringLiteral("TEC电源关"), 0x1F, 0xA0);
    button(2, QStringLiteral("探测器增益开"), 0x2C, 0xFF);
    button(3, QStringLiteral("探测器增益关"), 0x2C, 0xF0); finishRow();

    title(QStringLiteral("算法与图像控制"));
    button(0, QStringLiteral("两点开"), 0x02, 0xFF);
    button(1, QStringLiteral("两点关"), 0x02, 0xF0);
    button(2, QStringLiteral("坏点开"), 0x03, 0xFF);
    button(3, QStringLiteral("坏点关"), 0x03, 0xF0);
    button(4, QStringLiteral("自动积分开"), 0x20, 0xFF);
    button(5, QStringLiteral("自动积分关"), 0x20, 0xF0); finishRow();

    button(0, QStringLiteral("两点G"), 0x02, 0xEE);
    button(1, QStringLiteral("两点O"), 0x02, 0xE0);
    button(2, QStringLiteral("中心ROI"), 0x28, 0xFF);
    button(3, QStringLiteral("全幅统计"), 0x28, 0xF0);
    button(4, QStringLiteral("自动温度开"), 0x36, 0xFF);
    button(5, QStringLiteral("自动温度关"), 0x36, 0xF0);
    finishRow();

    button(0, QStringLiteral("中值开"), 0x11, 0xFF);
    button(1, QStringLiteral("中值关"), 0x11, 0xF0);
    button(2, QStringLiteral("帧间开"), 0x10, 0xFF, 0, false);
    button(3, QStringLiteral("帧间关"), 0x10, 0xF0, 0, false);
    button(4, QStringLiteral("镜像开"), 0x09, 0xFF);
    button(5, QStringLiteral("镜像关"), 0x09, 0xF0); finishRow();

    button(0, QStringLiteral("直方图开"), 0x2D, 0xFF);
    button(1, QStringLiteral("直方图关"), 0x2D, 0xF0);
    button(2, QStringLiteral("线性拉伸开"), 0x27, 0xFF);
    button(3, QStringLiteral("线性拉伸关"), 0x27, 0xF0);
    button(4, QStringLiteral("K+"), 0x25, 0xFF);
    button(5, QStringLiteral("K-"), 0x25, 0xF0); finishRow();

    button(2, QStringLiteral("B+"), 0x26, 0xFF);
    button(3, QStringLiteral("B-"), 0x26, 0xF0);
    button(4, QStringLiteral("锐度开"), 0x31, 0xFF);
    button(5, QStringLiteral("锐度关"), 0x31, 0xF0); finishRow();

    button(0, QStringLiteral("测速模式开"), 0x2B, 0xFF);
    button(1, QStringLiteral("测速模式关"), 0x2B, 0xF0); finishRow();

    title(QStringLiteral("两点参数区域"));
    const QStringList temperatureGroups = {QStringLiteral("低温"), QStringLiteral("中温"), QStringLiteral("高温")};
    for (int group = 0; group < 3; ++group) {
        layout->addWidget(new QLabel(temperatureGroups[group], panel), row, 0);
        for (int index = 0; index < 5; ++index) {
            const int region = group * 5 + index + 1;
            auto *regionButton = new QPushButton(QString::number(region), panel);
            regionButton->setCheckable(true);
            regionButton->setToolTip(QStringLiteral("区域%1：%2第%3套积分表")
                                     .arg(region).arg(temperatureGroups[group]).arg(index + 1));
            connect(regionButton, &QPushButton::clicked, this, [this, region]() {
                emitCommand(0x12, 0xFF, quint16(region - 1));
            });
            m_regionButtons.insert(region, regionButton);
            layout->addWidget(regionButton, row, index + 1);
        }
        finishRow();
    }

    title(QStringLiteral("十字线"));
    button(0, QStringLiteral("十字线开"), 0x21, 0xFF); button(1, QStringLiteral("十字线关"), 0x21, 0xF0);
    button(2, QStringLiteral("X+"), 0x22, 0xFF); button(3, QStringLiteral("X-"), 0x22, 0xF0);
    button(4, QStringLiteral("Y+"), 0x23, 0xF0); button(5, QStringLiteral("Y-"), 0x23, 0xFF); finishRow();
    m_crossX = new QSpinBox(panel); m_crossX->setRange(0, 4095); m_crossX->setValue(1024);
    m_crossY = new QSpinBox(panel); m_crossY->setRange(0, 4095); m_crossY->setValue(1024);
    layout->addWidget(new QLabel(QStringLiteral("X"), panel), row, 0); layout->addWidget(m_crossX, row, 1);
    layout->addWidget(new QLabel(QStringLiteral("Y"), panel), row, 2); layout->addWidget(m_crossY, row, 3);
    auto *coord = new QPushButton(QStringLiteral("设置坐标"), panel);
    connect(coord, &QPushButton::clicked, this, [this]() {
        emit commandRequested(UartProtocol::makeCrosshairCommand(quint16(m_crossX->value()), quint16(m_crossY->value())));
    });
    layout->addWidget(coord, row, 4); finishRow();

    title(QStringLiteral("校正与固化"));
    button(0, QStringLiteral("暗场采集"), 0x01, 0xFF); button(1, QStringLiteral("亮场采集"), 0x01, 0xF0);
    button(2, QStringLiteral("固化配置"), 0x0B, 0xFF); button(3, QStringLiteral("固化两点"), 0x0C, 0xFF);
    finishRow();
    title(QStringLiteral("原始HEX发送"));
    m_rawCommand = new QLineEdit(panel);
    m_rawCommand->setPlaceholderText(QStringLiteral("EA 01 CMD CTRL DATA_H DATA_L CHECKSUM 0A"));
    layout->addWidget(m_rawCommand, row, 0, 1, 5);
    auto *rawSend = new QPushButton(QStringLiteral("发送"), panel);
    connect(rawSend, &QPushButton::clicked, this, [this]() {
        QByteArray raw = QByteArray::fromHex(m_rawCommand->text().toLatin1());
        if (raw.isEmpty()) QMessageBox::warning(this, QStringLiteral("串口指令"), QStringLiteral("HEX数据无效"));
        else emit commandRequested(raw);
    });
    layout->addWidget(rawSend, row, 5);

    panel->setLayout(layout);
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    return scroll;
}

void TelemetryDebugDialog::emitCommand(quint8 code, quint8 control, quint16 value)
{
    emit commandRequested(UartProtocol::makeCommand(code, control, value));
}

void TelemetryDebugDialog::handleFrame(const TelemetryFrame &f)
{
    setValue("time", f.timestamp.toString("yyyy-MM-dd HH:mm:ss.zzz"));
    setValue("int", QStringLiteral("%1 ms (raw=%2)").arg(f.intTime / 100.0, 0, 'f', 2).arg(f.intTime));
    setValue("metric", QString::number(f.frameMetric));
    setValue("board", QStringLiteral("%1 ℃ (%2)").arg(UartProtocol::ds18b20Temperature(f.boardTempRaw), 0, 'f', 2).arg(hex16(f.boardTempRaw)));
    setValue("itec", QStringLiteral("%1 A").arg(f.itecRaw / 1000.0, 0, 'f', 3));
    setValue("vtec", QStringLiteral("%1 V").arg(f.vtecRaw / 1000.0, 0, 'f', 3));
    setValue("actual", tecMonitorText(f.tmpActualRaw, f.tecPowerEnabled()));
    setValue("set", tecMonitorText(f.tmpSetRaw, f.tecPowerEnabled()));
    setValue("sharp", QString::number(f.sharpness));
    setValue("cross", QStringLiteral("%1, %2").arg(f.crosshairX).arg(f.crosshairY));
    setValue("com", QStringLiteral("%1 V (%2)")
             .arg(UartProtocol::comDacCodeToVoltage(f.comSetRaw), 0, 'f', 3)
             .arg(hex16(f.comSetRaw)));
    setValue("kb", QStringLiteral("K=%1, B=%2").arg(f.linearKLevel).arg(f.linearBLevel));
    const int region = int(f.tpRegion) + 1;
    setValue("tp_region", QString::number(region));
    const int activeRegion = int(f.autoRegion) + 1;
    setValue("auto_region", QStringLiteral("区域%1 / %2 ms (raw=%3)")
             .arg(activeRegion)
             .arg(f.intTime / 100.0, 0, 'f', 2)
             .arg(f.intTime));
    const QStringList groupNames = {
        QStringLiteral("低温档（-10℃表）"),
        QStringLiteral("中温档（15℃表）"),
        QStringLiteral("高温档（40℃表）")
    };
    const QString groupText = f.temperatureGroup < groupNames.size()
        ? groupNames.at(f.temperatureGroup)
        : QStringLiteral("未知档位%1").arg(f.temperatureGroup);
    setValue("temp_group", QStringLiteral("%1 / 自动选择%2")
             .arg(groupText, f.autoTemperatureEnabled() ? QStringLiteral("开启")
                                                        : QStringLiteral("关闭")));
    for (auto it = m_regionButtons.cbegin(); it != m_regionButtons.cend(); ++it) {
        const bool manualSelected = it.key() == region;
        const bool actuallyActive = it.key() == activeRegion;
        it.value()->setChecked(manualSelected);
        QStringList styles;
        if (manualSelected)
            styles << QStringLiteral("background:#16803c; color:white; font-weight:600");
        if (actuallyActive)
            styles << QStringLiteral("border:2px solid #2878c8");
        it.value()->setStyleSheet(styles.isEmpty()
            ? QString()
            : QStringLiteral("QPushButton { %1; }").arg(styles.join(QStringLiteral("; "))));
    }

    const QString separator = QStringLiteral("&nbsp;&nbsp;&nbsp;");
    setValue("algo0", QStringList({
        statusMark(QStringLiteral("自动积分"), f.autoExposure()),
        statusMark(QStringLiteral("中心ROI"), f.autoRoiEnabled()),
        statusMark(QStringLiteral("TEC"), f.tecEnabled()),
        statusMark(QStringLiteral("TEC电源"), f.tecPowerEnabled()),
        statusMark(QStringLiteral("两点"), f.twoPointEnabled()),
        statusMark(QStringLiteral("坏点"), f.badPixelEnabled()),
        statusMark(QStringLiteral("中值"), f.medianEnabled()),
        statusMark(QStringLiteral("直方图"), f.histogramEnabled()),
        statusMark(QStringLiteral("镜像"), f.flipEnabled())
    }).join(separator));
    setValue("algo1", QStringList({
        statusMark(QStringLiteral("线性拉伸"), f.stretchEnabled()),
        statusMark(QStringLiteral("十字线"), f.crosshairEnabled()),
        statusMark(QStringLiteral("锐度"), f.sharpnessEnabled()),
        statusMark(QStringLiteral("探测器增益"), f.gainEnabled()),
        statusMark(QStringLiteral("测速"), f.transferEnabled()),
        statusMark(QStringLiteral("自动温度"), f.autoTemperatureEnabled()),
        statusMark(QStringLiteral("帧间"), f.iffEnabled(), false)
    }).join(separator));
    setValue("version", QStringLiteral("V%1.%2 / %3-%4-%5")
             .arg(f.version0).arg(f.version1).arg(f.versionYear).arg(f.versionMonth).arg(f.versionDay));
    setValue("flash", QStringList({
        completionMark(QStringLiteral("初始化"), f.flashInitDone()),
        completionMark(QStringLiteral("配置固化"), f.configSaveDone()),
        completionMark(QStringLiteral("两点固化"), f.twoPointSaveDone())
    }).join(separator));
    if (m_csvFile.isOpen()) writeCsvRow(f);
}

void TelemetryDebugDialog::addChecksumErrors(int count)
{
    m_checksumErrors += count;
    m_errorLabel->setText(QStringLiteral("校验错误：%1").arg(m_checksumErrors));
}

void TelemetryDebugDialog::setValue(const QString &key, const QString &value)
{
    if (auto *label = m_labels.value(key, nullptr)) label->setText(value);
}

void TelemetryDebugDialog::chooseCsvFile()
{
    m_csvPath = QFileDialog::getSaveFileName(this, QStringLiteral("保存遥测CSV"), QString(), QStringLiteral("CSV (*.csv)"));
    if (!m_csvPath.isEmpty()) m_csvLabel->setText(m_csvPath);
}

void TelemetryDebugDialog::toggleCsv()
{
    if (m_csvFile.isOpen()) {
        m_csvFile.close(); m_csvButton->setText(QStringLiteral("开始记录")); return;
    }
    if (m_csvPath.isEmpty()) chooseCsvFile();
    if (m_csvPath.isEmpty()) return;
    m_csvFile.setFileName(m_csvPath);
    if (!m_csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&m_csvFile);
    out << "timestamp,int_time,frame_metric,board_temp_raw,itec,vtec,tmp_actual,tmp_set,sharpness,cross_x,cross_y,com_set,k,b,tp_region,temp_group,auto_region,status0,status1,version0,version1,year,month,day,flash\n";
    m_csvButton->setText(QStringLiteral("停止记录"));
}

void TelemetryDebugDialog::writeCsvRow(const TelemetryFrame &f)
{
    QTextStream out(&m_csvFile);
    out << f.timestamp.toString(Qt::ISODateWithMs) << ',' << f.intTime << ',' << f.frameMetric << ',' << f.boardTempRaw << ','
        << f.itecRaw << ',' << f.vtecRaw << ',' << f.tmpActualRaw << ',' << f.tmpSetRaw << ','
        << f.sharpness << ',' << f.crosshairX << ',' << f.crosshairY << ',' << f.comSetRaw << ','
        << f.linearKLevel << ',' << f.linearBLevel << ',' << f.tpRegion << ','
        << f.temperatureGroup << ',' << f.autoRegion << ',' << f.status0 << ',' << f.status1 << ',' << f.version0 << ','
        << f.version1 << ',' << f.versionYear << ',' << f.versionMonth << ',' << f.versionDay << ','
        << f.flashStatus << '\n';
    m_csvFile.flush();
}
