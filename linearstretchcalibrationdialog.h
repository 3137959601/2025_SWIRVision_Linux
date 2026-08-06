#ifndef LINEARSTRETCHCALIBRATIONDIALOG_H
#define LINEARSTRETCHCALIBRATIONDIALOG_H

#include <QDateTime>
#include <QDialog>
#include <QElapsedTimer>
#include <QVector>
#include <array>

#include "linearstretchmath.h"
#include "uartprotocol.h"

class QCheckBox;
class QDoubleSpinBox;
class QImage;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;

class LinearStretchCalibrationDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LinearStretchCalibrationDialog(QWidget *parent = nullptr);

public slots:
    void handleTelemetryFrame(const TelemetryFrame &frame);
    void handleFrameAvailable();

private:
    enum class CaptureType { None, Dark, Bright };

    struct RegionRecord {
        bool darkValid = false;
        bool brightValid = false;
        double darkMean = 0.0;
        double darkFrameStd = 0.0;
        double brightMean = 0.0;
        double brightFrameStd = 0.0;
        int darkFrames = 0;
        int brightFrames = 0;
        quint16 darkIntTime = 0;
        quint16 brightIntTime = 0;
        double darkTemperature = 0.0;
        double brightTemperature = 0.0;
        double kRatio = 1.0;
        double bOffset = 0.0;
    };

    struct FrameStats {
        int imageWidth = 0;
        int imageHeight = 0;
        int roiX = 0;
        int roiY = 0;
        int roiWidth = 0;
        int roiHeight = 0;
        double mean = 0.0;
        double pixelStd = 0.0;
        quint16 minimum = 0;
        quint16 maximum = 0;
        double saturationPercent = 0.0;
    };

    struct SampleRecord {
        QDateTime timestamp;
        int region = -1;
        CaptureType type = CaptureType::None;
        int sampleIndex = 0;
        FrameStats stats;
        quint16 intTime = 0;
        double temperature = 0.0;
    };

    void buildUi();
    void beginCapture(CaptureType type);
    void finishCapture();
    void cancelCapture();
    bool readFrame(FrameStats &stats, QImage *preview);
    void updateTelemetryStatus();
    void updateRegionControls();
    void updateFormulaDisplay();
    void updateTable();
    void updateTableRow(int region);
    void updateCaptureButtons();
    void clearCurrentRegion();
    void clearAllRegions();
    void exportCsv();
    LinearStretchMath::Parameters baseParameters(int region) const;
    LinearStretchMath::Parameters finalParameters(int region) const;

    std::array<RegionRecord, 15> m_regions{};
    QVector<SampleRecord> m_samples;
    QVector<double> m_captureMeans;
    CaptureType m_captureType = CaptureType::None;
    int m_captureRegion = -1;

    int m_currentRegion = -1;
    quint16 m_currentIntTime = 0;
    double m_currentTemperature = 0.0;
    quint8 m_currentKLevel = 0;
    quint8 m_currentBLevel = 0;
    bool m_twoPointEnabled = false;
    bool m_stretchEnabled = false;
    bool m_histogramEnabled = false;
    bool m_haveTelemetry = false;
    bool m_loadingControls = false;
    QElapsedTimer m_previewTimer;

    QLabel *m_regionLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_frameLabel = nullptr;
    QLabel *m_liveStatsLabel = nullptr;
    QLabel *m_formulaLabel = nullptr;
    QLabel *m_previewLabel = nullptr;
    QCheckBox *m_fullFrame = nullptr;
    QSpinBox *m_roiX = nullptr;
    QSpinBox *m_roiY = nullptr;
    QSpinBox *m_roiWidth = nullptr;
    QSpinBox *m_roiHeight = nullptr;
    QSpinBox *m_sampleFrames = nullptr;
    QSpinBox *m_targetBlack = nullptr;
    QSpinBox *m_targetWhite = nullptr;
    QDoubleSpinBox *m_kRatio = nullptr;
    QDoubleSpinBox *m_bOffset = nullptr;
    QPushButton *m_captureDark = nullptr;
    QPushButton *m_captureBright = nullptr;
    QPushButton *m_cancelCapture = nullptr;
    QProgressBar *m_captureProgress = nullptr;
    QTableWidget *m_table = nullptr;
};

#endif // LINEARSTRETCHCALIBRATIONDIALOG_H
