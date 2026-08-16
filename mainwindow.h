#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QCheckBox>
#include <QLineEdit>
#include <QXmlStreamReader>

#include "common/tih_usb_device.h"
#include "transfer_thread.h"
#include "common/myCombox/myCombox.h"
#include "widget_image.h"
#include "gl_image_widget.h"
#include "image_processor.h"
#include "offline_replay_worker.h"
#include "serialworker.h"

#include <QImage>
#include <memory>
#include "qthread.h"
#include <QButtonGroup>
#include <QFileInfo>
#include <QFileDialog>
#include <QtEndian>     // 用于大小端安全
#include <QDir>
#include <QSerialPort>
#include <QSerialPortInfo>

#include <QDockWidget>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QScrollArea>
#include <QRadioButton>
#include <QLabel>
#include <QSlider>
#include <QAction>

namespace Ui {
class MainWindow;
}
class TelemetryDebugDialog;
class LinearStretchCalibrationDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    bool startOfflineReplayFile(const QString &filePath, int width, int height,
                                double framesPerSecond, bool loop,
                                int outputBits = 16, bool showDialogs = false);
    void stopOfflineReplay();
    bool saveCurrentFrame(const QString &filePath);
    QThread *thread1;
    QThread *thread2;
    ImageProcessor *imgProc = nullptr;
    SerialWorker *serialworker;
    QThread *serialThread = nullptr;
//    QButtonGroup* bgGroup1;
//    QButtonGroup* bgGroup2;


    static bool b_frame_save;
    void initUI();
    void initBPM();

    void initImageProcessing();
    void initSerial();
    //上位机算法
    void on_medianblur_radio_sel();//中值滤波
    void on_EqualizeHist_sel();//直方图均衡
    void on_TwoPointCorrect_sel();
    void on_DarkestOffsetCorrect_sel();
    void on_BlindPointDetect_sel();
    //FPGA串口指令
    void on_TEC_Power_sel();//TEC电源
    void on_TEC_Enable_sel();//TEC使能开关
    void on_AE_sel();//automatic exposure 自动曝光开关
    void on_SGAIN_sel();//探测器增益开关
    void on_FPGA_TwoPointCorrect_sel();
    void on_FPGA_BlindPointDetect_sel();
    void on_FPGA_Enhance_sel(); //暂定为线性增强
signals:
    void offlineFrameProcessed(quint64 frameIndex);
    void offlineReplayFailed(const QString &message);
    void offlineReplayEnded(quint64 decodedFrames, bool canceled, bool success);
    //串口信号
    void open_serial_signal(QString com_name);
    void close_serial_signal();
    void serial_send_signal(QString buf);
    //指令参数控制
    void InstructSettings_signal(unsigned char flag,QList<float>value);
private slots:
    void transferRate();
    void on_pushButton_retrieve_clicked();
    void on_pushButton_connect_clicked();
    void on_pushButton_start_clicked();
    void timerMonitor();

    void on_FrameSavepB_clicked();
    void applySpec();

    //串口功能按键
    void on_serialpB_clicked();
    void on_serial_det_pB_clicked();
    //串口指令控制窗口

    void on_comv_pB_clicked();
    void on_Int_pB_clicked();
    void on_TECSet_pB_clicked();
    void Int_LCDNumShow_slot(float time);
    void BoardTemp_LCDNumShow_slot(float temp);
    void TECTemp_LCDNumShow_slot(std::vector<float>(temp));
    void Sharpness_LCDNumShow_slot(int value);
    void on_collect_brightfield_pB_clicked();

    void on_collect_darkfield_pB_clicked();

    void on_DataStreamSavepB_clicked();

    void on_save_pathtB_clicked();

    void on_twoPointsFixPB_clicked();

    void on_configFixpB_clicked();

    void on_paramReadpB_clicked();

private:
    Ui::MainWindow *ui;

    void clearSatus();
    void xmlUnpack(QXmlStreamReader *xml, QString &head);
    void loadXml();
    void setupOfflineReplayUi();
    void selectOfflineReplayFile();
    void startOfflineReplayFromUi();
    void configureOfflineFrameBuffers(int width, int height, int outputBits);

    typedef struct usbParam {
        uint16_t vid;
        uint16_t pid;
    } usbParam_t;
    tihUSBDevice   *usbSkeleton = NULL;
    transferThread *xferThread[8] = {NULL};
    std::shared_ptr<swir::usb::FrameAssembler> m_usbFrameAssembler;

    myCombox *comboxDevice = NULL;
    QString currentDevice;

    QCheckBox ckbox_eps[8];
    QLineEdit ledit_eps[8];

    QTimer timer;
    QTimer monitor;

    QList<usbParam_t> usbVidPids;

    widget_image *m_imageWidget = nullptr;
    GLImageWidget *m_glView = nullptr;

    // Dock 及控件指针（非 UI 文件生成）
    QDockWidget*  dockImageProcessing = nullptr;

    // 两点校正（NUC）
    QGroupBox*    grpNUC = nullptr;
    QPushButton*  btnLowRef = nullptr;
    QPushButton*  btnHighRef = nullptr;
    QPushButton*  btnClearCalib = nullptr;
    QButtonGroup* nucGroup = nullptr;          // on/off 组
    QRadioButton* rbNUC_On = nullptr;
    QRadioButton* rbNUC_Off = nullptr;

    // 最暗场补偿校正
    QGroupBox*    grpDarkestOffset = nullptr;
    QPushButton*  btnDarkestRef = nullptr;
    QRadioButton* rbDarkestOffset_On = nullptr;
    QRadioButton* rbDarkestOffset_Off = nullptr;

    // 盲元去除
    QGroupBox*    grpBlind = nullptr;
    QButtonGroup* blindGroup = nullptr;   // -2=开, -3=关
    QRadioButton* rbBlindOn  = nullptr;
    QRadioButton* rbBlindOff = nullptr;

    // 中值滤波
    QGroupBox*    grpMedian = nullptr;
    QButtonGroup* medianGroup = nullptr;
    QRadioButton* rbMedian_On = nullptr;
    QRadioButton* rbMedian_Off = nullptr;

    // 直方图均衡
    QGroupBox*    grpHistEq = nullptr;
    QButtonGroup* histEqGroup = nullptr;
    QRadioButton* rbHistEq_On = nullptr;
    QRadioButton* rbHistEq_Off = nullptr;
    QSlider*      sldHistUpper = nullptr;
    QSlider*      sldHistLower = nullptr;
    QLabel*       lblHistUpperValue = nullptr;
    QLabel*       lblHistLowerValue = nullptr;
    QCheckBox*    chkHistDownsample = nullptr;
    // ==== BPM 参数面板控件 ====
    QDockWidget   *dockBpm = nullptr;
    QDoubleSpinBox *sbGainMin = nullptr, *sbGainMax = nullptr, *sbDsnuKmad = nullptr;
    QSpinBox      *sbDsnuAbs = nullptr, *sbLinResid = nullptr, *sbBlack = nullptr, *sbWhite = nullptr;
    QCheckBox     *chkAutoRebuild = nullptr;

    // 状态栏显示像素信息 x/y/DN
    QLabel* hoverLabel = nullptr;
    QLabel* srcFpsLabel = nullptr;      //图像处理帧率
    QLabel* dispFpsLabel = nullptr;     //图像显示实际帧率

    // 统一保存配置（由 save_pathtB 设置）
    QString m_imageSaveDir;
    QString m_streamSaveDir;
    QString m_imageSaveExt = "raw";
    bool m_streamSaving = false;
    OfflineReplayWorker *m_offlineReplay = nullptr;
    QString m_offlineReplayPath;
    QAction *m_offlineChooseAction = nullptr;
    QAction *m_offlineStartAction = nullptr;
    QAction *m_offlineStopAction = nullptr;
    QAction *m_offlineLoopAction = nullptr;
    bool m_offlineFramePending = false;
    quint64 m_pendingOfflineFrameIndex = 0;
    bool m_offlineShowDialogs = false;
    TelemetryDebugDialog *m_telemetryDialog = nullptr;
    LinearStretchCalibrationDialog *m_linearStretchDialog = nullptr;

};

#endif // MAINWINDOW_H
