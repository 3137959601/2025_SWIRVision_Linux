#include "linearstretchcalibrationdialog.h"

#include "widget_image.h"

#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QMutexLocker>
#include <QProgressBar>
#include <QPushButton>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <vector>

namespace {

QString regionName(int region)
{
    if (region < 0 || region >= 15)
        return QStringLiteral("--");
    static const QStringList groups = {
        QStringLiteral("低温"), QStringLiteral("中温"), QStringLiteral("高温")
    };
    return QStringLiteral("区域%1（%2第%3档）")
        .arg(region + 1).arg(groups.at(region / 5)).arg(region % 5 + 1);
}

QString numberOrDash(bool valid, double value, int precision = 3)
{
    return valid ? QString::number(value, 'f', precision) : QStringLiteral("--");
}

QString captureTypeName(int type)
{
    return type == 1 ? QStringLiteral("dark") : QStringLiteral("bright");
}

} // namespace

LinearStretchCalibrationDialog::LinearStretchCalibrationDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("线性拉伸标定"));
    setModal(false);
    resize(1280, 900);
    buildUi();
    m_previewTimer.start();
    updateTelemetryStatus();
    updateTable();
    updateRegionControls();
}

void LinearStretchCalibrationDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto *statusBox = new QGroupBox(QStringLiteral("当前状态"), this);
    auto *statusLayout = new QGridLayout(statusBox);
    m_regionLabel = new QLabel(QStringLiteral("当前区域：--"), statusBox);
    m_statusLabel = new QLabel(statusBox);
    m_frameLabel = new QLabel(QStringLiteral("图像：--"), statusBox);
    m_liveStatsLabel = new QLabel(QStringLiteral("实时统计：--"), statusBox);
    m_liveStatsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusLayout->addWidget(m_regionLabel, 0, 0);
    statusLayout->addWidget(m_frameLabel, 0, 1);
    statusLayout->addWidget(m_statusLabel, 1, 0, 1, 2);
    statusLayout->addWidget(m_liveStatsLabel, 2, 0, 1, 2);
    root->addWidget(statusBox);

    auto *workRow = new QHBoxLayout;
    auto *controlColumn = new QVBoxLayout;

    auto *captureBox = new QGroupBox(QStringLiteral("亮暗场统计"), this);
    auto *captureLayout = new QGridLayout(captureBox);
    m_fullFrame = new QCheckBox(QStringLiteral("全幅统计"), captureBox);
    m_fullFrame->setChecked(true);
    m_roiX = new QSpinBox(captureBox);
    m_roiY = new QSpinBox(captureBox);
    m_roiWidth = new QSpinBox(captureBox);
    m_roiHeight = new QSpinBox(captureBox);
    for (QSpinBox *spin : {m_roiX, m_roiY, m_roiWidth, m_roiHeight})
        spin->setRange(0, 8192);
    m_roiWidth->setRange(1, 8192);
    m_roiHeight->setRange(1, 8192);
    m_roiX->setValue(512);
    m_roiY->setValue(512);
    m_roiWidth->setValue(1024);
    m_roiHeight->setValue(1024);
    m_sampleFrames = new QSpinBox(captureBox);
    m_sampleFrames->setRange(1, 128);
    m_sampleFrames->setValue(16);

    captureLayout->addWidget(m_fullFrame, 0, 0, 1, 2);
    captureLayout->addWidget(new QLabel(QStringLiteral("ROI X"), captureBox), 1, 0);
    captureLayout->addWidget(m_roiX, 1, 1);
    captureLayout->addWidget(new QLabel(QStringLiteral("ROI Y"), captureBox), 2, 0);
    captureLayout->addWidget(m_roiY, 2, 1);
    captureLayout->addWidget(new QLabel(QStringLiteral("ROI宽"), captureBox), 3, 0);
    captureLayout->addWidget(m_roiWidth, 3, 1);
    captureLayout->addWidget(new QLabel(QStringLiteral("ROI高"), captureBox), 4, 0);
    captureLayout->addWidget(m_roiHeight, 4, 1);
    captureLayout->addWidget(new QLabel(QStringLiteral("采集帧数"), captureBox), 5, 0);
    captureLayout->addWidget(m_sampleFrames, 5, 1);

    m_captureDark = new QPushButton(QStringLiteral("采集暗场"), captureBox);
    m_captureBright = new QPushButton(QStringLiteral("采集亮场"), captureBox);
    m_cancelCapture = new QPushButton(QStringLiteral("取消"), captureBox);
    m_cancelCapture->setEnabled(false);
    captureLayout->addWidget(m_captureDark, 6, 0);
    captureLayout->addWidget(m_captureBright, 6, 1);
    captureLayout->addWidget(m_cancelCapture, 6, 2);
    m_captureProgress = new QProgressBar(captureBox);
    m_captureProgress->setRange(0, m_sampleFrames->value());
    m_captureProgress->setValue(0);
    captureLayout->addWidget(m_captureProgress, 7, 0, 1, 3);
    controlColumn->addWidget(captureBox);

    auto *formulaBox = new QGroupBox(QStringLiteral("软件拉伸参数"), this);
    auto *formulaLayout = new QFormLayout(formulaBox);
    m_targetBlack = new QSpinBox(formulaBox);
    m_targetWhite = new QSpinBox(formulaBox);
    m_targetBlack->setRange(0, 8190);
    m_targetWhite->setRange(1, 8191);
    m_targetBlack->setValue(0);
    m_targetWhite->setValue(8191);
    m_kRatio = new QDoubleSpinBox(formulaBox);
    m_kRatio->setRange(0.5, 1.5);
    m_kRatio->setDecimals(4);
    m_kRatio->setSingleStep(0.01);
    m_kRatio->setValue(1.0);
    m_bOffset = new QDoubleSpinBox(formulaBox);
    m_bOffset->setRange(-4096.0, 4096.0);
    m_bOffset->setDecimals(1);
    m_bOffset->setSingleStep(16.0);
    m_bOffset->setValue(0.0);
    formulaLayout->addRow(QStringLiteral("目标黑电平"), m_targetBlack);
    formulaLayout->addRow(QStringLiteral("目标白电平"), m_targetWhite);
    formulaLayout->addRow(QStringLiteral("K倍率"), m_kRatio);
    formulaLayout->addRow(QStringLiteral("B加法偏移"), m_bOffset);
    m_formulaLabel = new QLabel(formulaBox);
    m_formulaLabel->setWordWrap(true);
    m_formulaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    formulaLayout->addRow(m_formulaLabel);
    controlColumn->addWidget(formulaBox);

    auto *fileRow = new QHBoxLayout;
    auto *save = new QPushButton(QStringLiteral("保存统计CSV"), this);
    auto *clearCurrent = new QPushButton(QStringLiteral("清除当前区域"), this);
    auto *clearAll = new QPushButton(QStringLiteral("清除全部"), this);
    fileRow->addWidget(save);
    fileRow->addWidget(clearCurrent);
    fileRow->addWidget(clearAll);
    controlColumn->addLayout(fileRow);
    controlColumn->addStretch(1);
    workRow->addLayout(controlColumn, 0);

    auto *previewBox = new QGroupBox(QStringLiteral("软件拉伸预览"), this);
    auto *previewLayout = new QVBoxLayout(previewBox);
    m_previewLabel = new QLabel(QStringLiteral("等待图像"), previewBox);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMinimumSize(600, 400);
    m_previewLabel->setStyleSheet(QStringLiteral("QLabel { background:#101010; color:#b0b0b0; border:1px solid #555; }"));
    previewLayout->addWidget(m_previewLabel, 1);
    workRow->addWidget(previewBox, 1);
    root->addLayout(workRow, 1);

    m_table = new QTableWidget(15, 16, this);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("区域"), QStringLiteral("温区/档位"),
        QStringLiteral("暗场均值"), QStringLiteral("暗场帧σ"), QStringLiteral("暗场帧数"),
        QStringLiteral("亮场均值"), QStringLiteral("亮场帧σ"), QStringLiteral("亮场帧数"),
        QStringLiteral("动态范围"), QStringLiteral("K基础"), QStringLiteral("B基础"),
        QStringLiteral("K倍率"), QStringLiteral("B偏移"),
        QStringLiteral("K最终"), QStringLiteral("B最终"), QStringLiteral("状态")
    });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setMinimumHeight(300);
    root->addWidget(m_table);

    const auto roiEnable = [this](bool full) {
        for (QSpinBox *spin : {m_roiX, m_roiY, m_roiWidth, m_roiHeight})
            spin->setEnabled(!full);
    };
    roiEnable(true);
    connect(m_fullFrame, &QCheckBox::toggled, this, roiEnable);
    connect(m_captureDark, &QPushButton::clicked, this, [this] { beginCapture(CaptureType::Dark); });
    connect(m_captureBright, &QPushButton::clicked, this, [this] { beginCapture(CaptureType::Bright); });
    connect(m_cancelCapture, &QPushButton::clicked, this, &LinearStretchCalibrationDialog::cancelCapture);
    connect(save, &QPushButton::clicked, this, &LinearStretchCalibrationDialog::exportCsv);
    connect(clearCurrent, &QPushButton::clicked, this, &LinearStretchCalibrationDialog::clearCurrentRegion);
    connect(clearAll, &QPushButton::clicked, this, &LinearStretchCalibrationDialog::clearAllRegions);

    const auto targetsChanged = [this] {
        if (m_targetWhite->value() <= m_targetBlack->value())
            m_targetWhite->setValue(std::min(8191, m_targetBlack->value() + 1));
        updateFormulaDisplay();
        updateTable();
    };
    connect(m_targetBlack, QOverload<int>::of(&QSpinBox::valueChanged), this, [targetsChanged](int) { targetsChanged(); });
    connect(m_targetWhite, QOverload<int>::of(&QSpinBox::valueChanged), this, [targetsChanged](int) { targetsChanged(); });
    connect(m_kRatio, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_loadingControls || m_currentRegion < 0) return;
        m_regions[size_t(m_currentRegion)].kRatio = value;
        updateFormulaDisplay(); updateTableRow(m_currentRegion);
    });
    connect(m_bOffset, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_loadingControls || m_currentRegion < 0) return;
        m_regions[size_t(m_currentRegion)].bOffset = value;
        updateFormulaDisplay(); updateTableRow(m_currentRegion);
    });
}

void LinearStretchCalibrationDialog::handleTelemetryFrame(const TelemetryFrame &frame)
{
    const int newRegion = frame.autoRegion < 15 ? int(frame.autoRegion) : -1;
    if (m_captureType != CaptureType::None && newRegion != m_captureRegion)
        cancelCapture();

    const bool regionChanged = newRegion != m_currentRegion;
    m_currentRegion = newRegion;
    m_currentIntTime = frame.intTime;
    m_currentTemperature = UartProtocol::ds18b20Temperature(frame.boardTempRaw);
    m_currentKLevel = frame.linearKLevel;
    m_currentBLevel = frame.linearBLevel;
    m_twoPointEnabled = frame.twoPointEnabled();
    m_stretchEnabled = frame.stretchEnabled();
    m_histogramEnabled = frame.histogramEnabled();
    m_haveTelemetry = true;
    updateTelemetryStatus();
    if (regionChanged) updateRegionControls();
    updateCaptureButtons();
}

void LinearStretchCalibrationDialog::handleFrameAvailable()
{
    if (!isVisible() && m_captureType == CaptureType::None)
        return;
    const bool previewDue = !m_previewTimer.isValid() || m_previewTimer.elapsed() >= 250;
    if (!previewDue && m_captureType == CaptureType::None)
        return;

    FrameStats stats;
    QImage preview;
    if (!readFrame(stats, previewDue ? &preview : nullptr))
        return;

    m_frameLabel->setText(QStringLiteral("图像：%1×%2，ROI %3,%4 %5×%6")
                          .arg(stats.imageWidth).arg(stats.imageHeight)
                          .arg(stats.roiX).arg(stats.roiY).arg(stats.roiWidth).arg(stats.roiHeight));
    m_liveStatsLabel->setText(QStringLiteral("实时13bit：均值 %1，像素σ %2，最小 %3，最大 %4，饱和 %5%")
                              .arg(stats.mean, 0, 'f', 3).arg(stats.pixelStd, 0, 'f', 3)
                              .arg(stats.minimum).arg(stats.maximum)
                              .arg(stats.saturationPercent, 0, 'f', 4));

    if (previewDue && !preview.isNull()) {
        m_previewLabel->setPixmap(QPixmap::fromImage(preview).scaled(
            m_previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_previewTimer.restart();
    }

    if (m_captureType == CaptureType::None)
        return;

    SampleRecord sample;
    sample.timestamp = QDateTime::currentDateTime();
    sample.region = m_captureRegion;
    sample.type = m_captureType;
    sample.sampleIndex = m_captureMeans.size() + 1;
    sample.stats = stats;
    sample.intTime = m_currentIntTime;
    sample.temperature = m_currentTemperature;
    m_samples.append(sample);
    m_captureMeans.append(stats.mean);
    m_captureProgress->setValue(m_captureMeans.size());

    if (m_captureMeans.size() >= m_sampleFrames->value())
        finishCapture();
}

bool LinearStretchCalibrationDialog::readFrame(FrameStats &stats, QImage *preview)
{
    std::vector<quint16> frame;
    int width = 0, height = 0, stride = 0;
    {
        QMutexLocker lock(&widget_image::s_imgMutex);
        width = widget_image::rawWidth();
        height = widget_image::rawHeight();
        stride = widget_image::rawStridePx();
        const quint16 *source = widget_image::rawPtr();
        if (!source || width <= 0 || height <= 0 || stride < width)
            return false;
        frame.resize(size_t(width) * size_t(height));
        for (int y = 0; y < height; ++y)
            std::memcpy(frame.data() + size_t(y) * width,
                        source + size_t(y) * stride, size_t(width) * sizeof(quint16));
    }

    int x0 = 0, y0 = 0, roiWidth = width, roiHeight = height;
    if (!m_fullFrame->isChecked()) {
        x0 = qBound(0, m_roiX->value(), width - 1);
        y0 = qBound(0, m_roiY->value(), height - 1);
        roiWidth = qBound(1, m_roiWidth->value(), width - x0);
        roiHeight = qBound(1, m_roiHeight->value(), height - y0);
    }

    quint64 sum = 0;
    long double sumSquares = 0.0;
    quint16 minimum = 8191;
    quint16 maximum = 0;
    quint64 saturated = 0;
    const quint64 count = quint64(roiWidth) * quint64(roiHeight);
    for (int y = y0; y < y0 + roiHeight; ++y) {
        const quint16 *row = frame.data() + size_t(y) * width;
        for (int x = x0; x < x0 + roiWidth; ++x) {
            const quint16 value = LinearStretchMath::usbWordToDn13(row[x]);
            sum += value;
            sumSquares += static_cast<long double>(value) * static_cast<long double>(value);
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            if (value >= 8190) ++saturated;
        }
    }

    stats.imageWidth = width;
    stats.imageHeight = height;
    stats.roiX = x0;
    stats.roiY = y0;
    stats.roiWidth = roiWidth;
    stats.roiHeight = roiHeight;
    stats.mean = double(sum) / double(count);
    const long double variance = std::max<long double>(0.0, sumSquares / count - stats.mean * stats.mean);
    stats.pixelStd = std::sqrt(double(variance));
    stats.minimum = minimum;
    stats.maximum = maximum;
    stats.saturationPercent = 100.0 * double(saturated) / double(count);

    if (preview) {
        const int step = std::max(1, int(std::ceil(std::max(width / 640.0, height / 480.0))));
        const int previewWidth = (width + step - 1) / step;
        const int previewHeight = (height + step - 1) / step;
        QImage image(previewWidth, previewHeight, QImage::Format_Grayscale8);
        const auto parameters = finalParameters(m_currentRegion);
        for (int py = 0; py < previewHeight; ++py) {
            uchar *dst = image.scanLine(py);
            const int sourceY = std::min(height - 1, py * step);
            const quint16 *sourceRow = frame.data() + size_t(sourceY) * width;
            for (int px = 0; px < previewWidth; ++px) {
                const int sourceX = std::min(width - 1, px * step);
                const quint16 input = LinearStretchMath::usbWordToDn13(sourceRow[sourceX]);
                const quint16 output = LinearStretchMath::apply13(input, parameters);
                dst[px] = uchar((quint32(output) * 255u + 4095u) / 8191u);
            }
        }
        *preview = image;
    }
    return true;
}

void LinearStretchCalibrationDialog::beginCapture(CaptureType type)
{
    if (m_currentRegion < 0 || !m_twoPointEnabled || m_stretchEnabled || m_histogramEnabled)
        return;

    m_captureType = type;
    m_captureRegion = m_currentRegion;
    m_captureMeans.clear();
    for (int i = m_samples.size() - 1; i >= 0; --i) {
        if (m_samples.at(i).region == m_captureRegion && m_samples.at(i).type == type)
            m_samples.removeAt(i);
    }
    m_captureProgress->setRange(0, m_sampleFrames->value());
    m_captureProgress->setValue(0);
    updateCaptureButtons();
}

void LinearStretchCalibrationDialog::finishCapture()
{
    if (m_captureRegion < 0 || m_captureMeans.isEmpty()) {
        cancelCapture(); return;
    }
    const double mean = std::accumulate(m_captureMeans.cbegin(), m_captureMeans.cend(), 0.0) /
                        double(m_captureMeans.size());
    double variance = 0.0;
    for (double value : m_captureMeans) variance += (value - mean) * (value - mean);
    variance /= double(m_captureMeans.size());

    RegionRecord &record = m_regions[size_t(m_captureRegion)];
    if (m_captureType == CaptureType::Dark) {
        record.darkValid = true;
        record.darkMean = mean;
        record.darkFrameStd = std::sqrt(variance);
        record.darkFrames = m_captureMeans.size();
        record.darkIntTime = m_currentIntTime;
        record.darkTemperature = m_currentTemperature;
    } else {
        record.brightValid = true;
        record.brightMean = mean;
        record.brightFrameStd = std::sqrt(variance);
        record.brightFrames = m_captureMeans.size();
        record.brightIntTime = m_currentIntTime;
        record.brightTemperature = m_currentTemperature;
    }
    const int finishedRegion = m_captureRegion;
    m_captureType = CaptureType::None;
    m_captureRegion = -1;
    m_captureMeans.clear();
    updateCaptureButtons();
    updateTableRow(finishedRegion);
    updateFormulaDisplay();
}

void LinearStretchCalibrationDialog::cancelCapture()
{
    m_captureType = CaptureType::None;
    m_captureRegion = -1;
    m_captureMeans.clear();
    m_captureProgress->setValue(0);
    updateCaptureButtons();
}

void LinearStretchCalibrationDialog::updateTelemetryStatus()
{
    m_regionLabel->setText(QStringLiteral("当前区域：%1；积分 %2 ms；温度 %3 ℃；FPGA K/B档 %4/%5")
                           .arg(regionName(m_currentRegion))
                           .arg(m_currentIntTime / 100.0, 0, 'f', 2)
                           .arg(m_currentTemperature, 0, 'f', 2)
                           .arg(m_currentKLevel).arg(m_currentBLevel));
    QStringList errors;
    if (!m_haveTelemetry) errors << QStringLiteral("等待串口遥测");
    if (!m_twoPointEnabled) errors << QStringLiteral("两点校正未开启");
    if (m_stretchEnabled) errors << QStringLiteral("FPGA线性拉伸已开启");
    if (m_histogramEnabled) errors << QStringLiteral("FPGA直方图已开启");
    if (m_currentRegion < 0) errors << QStringLiteral("实际区域无效");
    if (errors.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("采集就绪"));
        m_statusLabel->setStyleSheet(QStringLiteral("QLabel { color:#16833a; font-weight:600; }"));
    } else {
        m_statusLabel->setText(QStringLiteral("暂不可采集：") + errors.join(QStringLiteral("；")));
        m_statusLabel->setStyleSheet(QStringLiteral("QLabel { color:#b3261e; font-weight:600; }"));
    }
}

void LinearStretchCalibrationDialog::updateRegionControls()
{
    m_loadingControls = true;
    if (m_currentRegion >= 0) {
        const RegionRecord &record = m_regions[size_t(m_currentRegion)];
        QSignalBlocker blockK(m_kRatio), blockB(m_bOffset);
        m_kRatio->setValue(record.kRatio);
        m_bOffset->setValue(record.bOffset);
    }
    m_loadingControls = false;
    updateFormulaDisplay();
    updateCaptureButtons();
}

LinearStretchMath::Parameters LinearStretchCalibrationDialog::baseParameters(int region) const
{
    if (region < 0 || region >= 15) return {};
    const RegionRecord &record = m_regions[size_t(region)];
    if (!record.darkValid || !record.brightValid) return {};
    return LinearStretchMath::calculate(record.darkMean, record.brightMean,
                                        m_targetBlack->value(), m_targetWhite->value());
}

LinearStretchMath::Parameters LinearStretchCalibrationDialog::finalParameters(int region) const
{
    if (region < 0 || region >= 15) return {};
    const RegionRecord &record = m_regions[size_t(region)];
    return LinearStretchMath::adjusted(baseParameters(region), record.kRatio, record.bOffset);
}

void LinearStretchCalibrationDialog::updateFormulaDisplay()
{
    const auto base = baseParameters(m_currentRegion);
    const auto final = finalParameters(m_currentRegion);
    if (!base.valid) {
        m_formulaLabel->setText(QStringLiteral("当前区域尚未完成亮暗场采集。\n"
                                               "K=(目标白-目标黑)/(亮场-暗场)\n"
                                               "B=目标黑-K×暗场"));
        return;
    }
    m_formulaLabel->setText(QStringLiteral("基础：K=%1，B=%2\n最终：K=%3，B=%4\n"
                                           "最终B=基础B×K倍率+B偏移")
                                .arg(base.k, 0, 'f', 7).arg(base.b, 0, 'f', 3)
                                .arg(final.k, 0, 'f', 7).arg(final.b, 0, 'f', 3));
}

void LinearStretchCalibrationDialog::updateTable()
{
    for (int region = 0; region < 15; ++region) updateTableRow(region);
}

void LinearStretchCalibrationDialog::updateTableRow(int region)
{
    if (region < 0 || region >= 15) return;
    const RegionRecord &record = m_regions[size_t(region)];
    const auto base = baseParameters(region);
    const auto final = finalParameters(region);
    const QString group = QStringLiteral("%1/%2")
        .arg(region < 5 ? QStringLiteral("低温") : region < 10 ? QStringLiteral("中温") : QStringLiteral("高温"))
        .arg(region % 5 + 1);
    const QStringList values = {
        QString::number(region + 1), group,
        numberOrDash(record.darkValid, record.darkMean), numberOrDash(record.darkValid, record.darkFrameStd),
        record.darkValid ? QString::number(record.darkFrames) : QStringLiteral("--"),
        numberOrDash(record.brightValid, record.brightMean), numberOrDash(record.brightValid, record.brightFrameStd),
        record.brightValid ? QString::number(record.brightFrames) : QStringLiteral("--"),
        numberOrDash(record.darkValid && record.brightValid, record.brightMean - record.darkMean),
        numberOrDash(base.valid, base.k, 7), numberOrDash(base.valid, base.b),
        QString::number(record.kRatio, 'f', 4), QString::number(record.bOffset, 'f', 1),
        numberOrDash(final.valid, final.k, 7), numberOrDash(final.valid, final.b),
        base.valid ? QStringLiteral("已完成") : record.darkValid || record.brightValid ? QStringLiteral("缺一组") : QStringLiteral("未采集")
    };
    for (int column = 0; column < values.size(); ++column) {
        auto *item = m_table->item(region, column);
        if (!item) { item = new QTableWidgetItem; m_table->setItem(region, column, item); }
        item->setText(values.at(column));
        if (column == 15)
            item->setForeground(base.valid ? QColor(22, 131, 58) : QColor(150, 80, 20));
    }
}

void LinearStretchCalibrationDialog::updateCaptureButtons()
{
    const bool idle = m_captureType == CaptureType::None;
    const bool valid = m_haveTelemetry && m_currentRegion >= 0 && m_twoPointEnabled &&
                       !m_stretchEnabled && !m_histogramEnabled;
    m_captureDark->setEnabled(idle && valid);
    m_captureBright->setEnabled(idle && valid);
    m_cancelCapture->setEnabled(!idle);
    m_sampleFrames->setEnabled(idle);
}

void LinearStretchCalibrationDialog::clearCurrentRegion()
{
    if (m_currentRegion < 0) return;
    m_regions[size_t(m_currentRegion)] = RegionRecord{};
    for (int i = m_samples.size() - 1; i >= 0; --i)
        if (m_samples.at(i).region == m_currentRegion) m_samples.removeAt(i);
    updateRegionControls(); updateTableRow(m_currentRegion);
}

void LinearStretchCalibrationDialog::clearAllRegions()
{
    if (QMessageBox::question(this, QStringLiteral("清除统计"),
                              QStringLiteral("确认清除15个区域的全部亮暗场统计？")) != QMessageBox::Yes)
        return;
    m_regions = {};
    m_samples.clear();
    cancelCapture(); updateRegionControls(); updateTable();
}

void LinearStretchCalibrationDialog::exportCsv()
{
    const QString summaryPath = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存线性拉伸统计"), QStringLiteral("linear_stretch_calibration.csv"),
        QStringLiteral("CSV (*.csv)"));
    if (summaryPath.isEmpty()) return;

    QFile summary(summaryPath);
    if (!summary.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), summary.errorString()); return;
    }
    QTextStream out(&summary);
    out << "region,temp_group,level,dark_mean13,dark_frame_std,dark_frames,dark_int_time_raw,dark_temp_c,"
           "bright_mean13,bright_frame_std,bright_frames,bright_int_time_raw,bright_temp_c,span13,"
           "target_black,target_white,k_base,b_base,k_ratio,b_offset,k_final,b_final\n";
    for (int region = 0; region < 15; ++region) {
        const RegionRecord &record = m_regions[size_t(region)];
        const auto base = baseParameters(region);
        const auto final = finalParameters(region);
        out << region + 1 << ',' << region / 5 << ',' << region % 5 + 1 << ','
            << (record.darkValid ? QString::number(record.darkMean, 'f', 6) : QString()) << ','
            << (record.darkValid ? QString::number(record.darkFrameStd, 'f', 6) : QString()) << ','
            << record.darkFrames << ',' << record.darkIntTime << ','
            << (record.darkValid ? QString::number(record.darkTemperature, 'f', 4) : QString()) << ','
            << (record.brightValid ? QString::number(record.brightMean, 'f', 6) : QString()) << ','
            << (record.brightValid ? QString::number(record.brightFrameStd, 'f', 6) : QString()) << ','
            << record.brightFrames << ',' << record.brightIntTime << ','
            << (record.brightValid ? QString::number(record.brightTemperature, 'f', 4) : QString()) << ','
            << (record.darkValid && record.brightValid ? QString::number(record.brightMean - record.darkMean, 'f', 6) : QString()) << ','
            << m_targetBlack->value() << ',' << m_targetWhite->value() << ','
            << (base.valid ? QString::number(base.k, 'f', 9) : QString()) << ','
            << (base.valid ? QString::number(base.b, 'f', 6) : QString()) << ','
            << QString::number(record.kRatio, 'f', 6) << ',' << QString::number(record.bOffset, 'f', 3) << ','
            << (final.valid ? QString::number(final.k, 'f', 9) : QString()) << ','
            << (final.valid ? QString::number(final.b, 'f', 6) : QString()) << '\n';
    }
    summary.close();

    const QFileInfo info(summaryPath);
    const QString samplePath = info.dir().filePath(info.completeBaseName() + QStringLiteral("_samples.csv"));
    QFile samples(samplePath);
    if (samples.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream detail(&samples);
        detail << "timestamp,region,type,sample_index,int_time_raw,temperature_c,roi_x,roi_y,roi_width,roi_height,"
                  "mean13,pixel_std13,min13,max13,saturation_percent\n";
        for (const SampleRecord &sample : m_samples) {
            detail << sample.timestamp.toString(Qt::ISODateWithMs) << ',' << sample.region + 1 << ','
                   << captureTypeName(sample.type == CaptureType::Dark ? 1 : 2) << ',' << sample.sampleIndex << ','
                   << sample.intTime << ',' << QString::number(sample.temperature, 'f', 4) << ','
                   << sample.stats.roiX << ',' << sample.stats.roiY << ',' << sample.stats.roiWidth << ',' << sample.stats.roiHeight << ','
                   << QString::number(sample.stats.mean, 'f', 6) << ',' << QString::number(sample.stats.pixelStd, 'f', 6) << ','
                   << sample.stats.minimum << ',' << sample.stats.maximum << ','
                   << QString::number(sample.stats.saturationPercent, 'f', 6) << '\n';
        }
    }
    QMessageBox::information(this, QStringLiteral("保存完成"),
                             QStringLiteral("汇总：%1\n逐帧：%2").arg(summaryPath, samplePath));
}
