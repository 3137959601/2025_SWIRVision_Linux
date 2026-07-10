#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "common/device.h"
#include "telemetrydebugdialog.h"
#include <QMessageBox>
#include <QTime>
#include <QtEndian>
#include <QSettings>
#include <QDebug>
#include <QPainter>
#include <QPixmap>
#include <QFile>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QMenuBar>

#define AVERAGE_POLL_SIZE   10
#define CHANNELS_NUM 8
uint64_t volatile g_transOk = 0;
uint64_t volatile g_transErr = 0;
bool volatile end_flag=true;
uint64_t s_ms[AVERAGE_POLL_SIZE] = {0};
uint32_t s_ms_idx = 0;
uint64_t s_nbytes[AVERAGE_POLL_SIZE] = {0};
uint32_t s_nbytes_idx = 0;

QFile speedFile("./speed_log.txt");

bool MainWindow::b_frame_save= false;
extern bool serial_bind_flag;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    initUI();   //除图像处理控件
    initSerial();   //初始化串口
    //初始化图像控件及线程
    initImageProcessing();

    connect(&timer, SIGNAL(timeout()), this, SLOT(transferRate()));
    connect(&monitor, SIGNAL(timeout()), this, SLOT(timerMonitor()));
    loadXml();
    speedFile.open(QIODevice::ReadWrite | QIODevice::Append);


    // 初始设置
    applySpec();

}

MainWindow::~MainWindow()
{
    if (serialThread) {
        if (serial_bind_flag && serialworker) {
            QMetaObject::invokeMethod(serialworker, "SerialClose", Qt::BlockingQueuedConnection);
        }
        serialThread->quit();
        serialThread->wait();
    }
    if (comboxDevice)
        delete comboxDevice;
    delete ui;
}

void MainWindow::initUI()
{
    this->setWindowTitle("SWIRVision");

//    // 在代码里创建scrollArea里创建图像窗口
//    widget_image *imageWidget = new widget_image(ui->scrollArea);
//    // 设置初始图像规格（例如 2048x2048 16bit）
//    imageWidget->setImageSpec(2048, 2048, QImage::Format_Grayscale16);
//    // 将其作为 scrollArea 的内容控件
//    ui->scrollArea->setWidget(imageWidget);
//    ui->scrollArea->setWidgetResizable(false); // 需要滚动条：false
//    // 如果想在别的地方访问，可以保存指针
//    this->m_imageWidget = imageWidget;

    // 1) 用 GL 控件替换图像视图
    auto *glView = new GLImageWidget(ui->scrollArea);
    glView->setImageSpec(2048, 2048, 16);   // 初始规格（后续 applySpec 会变）
    ui->scrollArea->setWidget(glView);
    hoverLabel = new QLabel(this);
    hoverLabel->setStyleSheet(
        "QLabel {"
        "  background:rgba(0,0,0,200);"
        "  color:#00FF90;"
        "  padding:2px 10px;"
        "  border-radius:8px;"
        "  font: 700 14px 'Consolas';"
        "  letter-spacing: 1px;"
        "}"
    );
    hoverLabel->setText(QStringLiteral("x=--  y=--  DN=--  AVG_ALL=--  AVG_ROI=--"));
    statusBar()->addPermanentWidget(hoverLabel);
//    srcFpsLabel  = new QLabel("Src --.- FPS", this);
//    dispFpsLabel = new QLabel("Disp --.- FPS", this);
//    statusBar()->addPermanentWidget(srcFpsLabel);
//    statusBar()->addPermanentWidget(dispFpsLabel);
    // 连接 GL 的信号到标签
    connect(glView, &GLImageWidget::hoverInfoChanged,
            this, [this](int x, int y, quint32 dn, bool valid,
                         double avgAllDn, bool avgAllValid,
                         double avgRoiDn, bool avgRoiValid){
        const QString avgAllText = avgAllValid ? QString::number(avgAllDn, 'f', 2) : QStringLiteral("--");
        const QString avgRoiText = avgRoiValid ? QString::number(avgRoiDn, 'f', 2) : QStringLiteral("--");
        if (valid) {
            hoverLabel->setText(QString("x=%1  y=%2  DN=%3  AVG_ALL=%4  AVG_ROI=%5")
                                .arg(x).arg(y).arg(dn).arg(avgAllText).arg(avgRoiText));
            hoverLabel->setStyleSheet("QLabel{background:rgba(0,0,0,200);color:#00FF90;padding:2px 10px;border-radius:8px;font:700 14px 'Consolas';}");
        } else {
            hoverLabel->setText(QString("x=-  y=-  DN=-  AVG_ALL=%1  AVG_ROI=%2")
                                .arg(avgAllText).arg(avgRoiText));
            hoverLabel->setStyleSheet("QLabel{background:rgba(0,0,0,140);color:#AAAAAA;padding:2px 10px;border-radius:8px;font:700 14px 'Consolas';}");
        }
    });
//    connect(glView, &GLImageWidget::displayFpsChanged, this,
//            [this](double fps){ dispFpsLabel->setText(QString("Disp %1 FPS").arg(fps,0,'f',1)); });
//    ui->scrollArea->setWidgetResizable(false);
    ui->scrollArea->setWidgetResizable(true); // 让 GL 视图跟随窗口
    // 统一背景为纯黑：scrollArea 及其 viewport 都设为黑
    ui->scrollArea->setStyleSheet("background-color: black;");
    ui->scrollArea->viewport()->setStyleSheet("background-color: black;");
    ui->scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);//关闭滚动条
    ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->m_glView = glView;

//    setFixedSize(this->width(), this->height());  //固定主窗口大小
    ui->pushButton_start->setEnabled(false);
    // 默认创建状态栏
    auto p_status_bar = this->statusBar();
    p_status_bar->showMessage("SWIRVision");

//    ui->loadBPMtB->setToolTip("加载BMP");
    comboxDevice = new myCombox(this);
    ui->horizontalLayout->addWidget(comboxDevice, 1);

    for (int i = 0; i < 8; i++) {
        ledit_eps[i].setFocusPolicy(Qt::FocusPolicy::NoFocus);
    }//设置不可获得焦点，视觉上即点击无法选中
    connect(ui->comboResolution, &QComboBox::currentTextChanged, this, [=](const QString&){ applySpec(); });
    connect(ui->comboFormat,     QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [=](int){ applySpec(); });

    ui->serialpB->setIcon(QIcon(":/icons/serial_close.png"));

    const QSize arrowSize(24, 24);
    ui->chUpShift_tB->setIcon(QIcon(":/icons/up_arrow.png"));
    ui->chDownShift_tB->setIcon(QIcon(":/icons/down_arrow.png"));
    ui->chLeftShift_tB->setIcon(QIcon(":/icons/left_arrow.png"));
    ui->chRightShift_tB->setIcon(QIcon(":/icons/right_arrow.png"));
    ui->chUpShift_tB->setIconSize(arrowSize);
    ui->chDownShift_tB->setIconSize(arrowSize);
    ui->chLeftShift_tB->setIconSize(arrowSize);
    ui->chRightShift_tB->setIconSize(arrowSize);
    ui->chUpShift_tB->setText(QString());
    ui->chDownShift_tB->setText(QString());
    ui->chLeftShift_tB->setText(QString());
    ui->chRightShift_tB->setText(QString());
    ui->chUpShift_tB->setAutoRaise(true);
    ui->chDownShift_tB->setAutoRaise(true);
    ui->chLeftShift_tB->setAutoRaise(true);
    ui->chRightShift_tB->setAutoRaise(true);
    ui->chUpShift_tB->setAutoRepeat(true);
    ui->chDownShift_tB->setAutoRepeat(true);
    ui->chLeftShift_tB->setAutoRepeat(true);
    ui->chRightShift_tB->setAutoRepeat(true);
    //按住会每 100ms 发送一次（按下后 300ms 开始连发）
    ui->chUpShift_tB->setAutoRepeatDelay(300);
    ui->chDownShift_tB->setAutoRepeatDelay(300);
    ui->chLeftShift_tB->setAutoRepeatDelay(300);
    ui->chRightShift_tB->setAutoRepeatDelay(300);
    ui->chUpShift_tB->setAutoRepeatInterval(100);
    ui->chDownShift_tB->setAutoRepeatInterval(100);
    ui->chLeftShift_tB->setAutoRepeatInterval(100);
    ui->chRightShift_tB->setAutoRepeatInterval(100);

    // 读取保存配置（统一用于纯图像保存/数据流保存）
    QSettings s("SWIRVision", "SWIRVision");
    m_imageSaveDir  = s.value("save/imageDir",  QDir::currentPath()).toString();
    m_streamSaveDir = m_imageSaveDir; // 数据流路径默认与图像路径一致
    m_imageSaveExt  = s.value("save/imageExt",  QString("raw")).toString().toLower();
    if (m_imageSaveExt != "raw" && m_imageSaveExt != "png" &&
        m_imageSaveExt != "tif" && m_imageSaveExt != "bmp") {
        m_imageSaveExt = "raw";
    }
    ui->save_pathtB->setToolTip(
        QString("图像/数据流: %1\n图像格式: .%2")
            .arg(m_imageSaveDir, m_imageSaveExt));
}
void MainWindow::initSerial()
{
    QStringList serialNamePort;
    for (const auto& it : QSerialPortInfo::availablePorts())
    {
        serialNamePort<<it.portName();
    }
    ui->serialCb->addItems(serialNamePort);

    serialworker = new SerialWorker;
    serialThread = new QThread(this);
    serialworker->moveToThread(serialThread);
    connect(serialThread, &QThread::finished, serialworker, &QObject::deleteLater);
    serialThread->start();

    connect(this,&MainWindow::open_serial_signal,serialworker,&SerialWorker::SerialPortInit, Qt::BlockingQueuedConnection);
    connect(this,&MainWindow::close_serial_signal,serialworker,&SerialWorker::SerialClose, Qt::BlockingQueuedConnection);

//    connect(serialworker,&SerialWorker::recvDataSignal,this,&MainWindow::serial_recvDataSlot);

    //发送指令编码
    connect(this,&MainWindow::InstructSettings_signal,serialworker,&SerialWorker::InstructionCode, Qt::QueuedConnection);
    //发送指令
    connect(serialworker,&SerialWorker::instruction_send_signal,serialworker,&SerialWorker::SerialSendData_Slot);
    //直接发送原始串口帧
    connect(this,&MainWindow::serial_send_signal,serialworker,&SerialWorker::SerialSendData_Slot, Qt::QueuedConnection);
    //接收指令信号槽在串口类内部
    //功能分组

    //TEC使能
    ui->TEC_PowerON_rB->setChecked(true);
    connect(ui->TEC_EnableON_rB,  &QRadioButton::clicked, this, &MainWindow::on_TEC_Enable_sel);
    connect(ui->TEC_EnableOFF_rB, &QRadioButton::clicked, this, &MainWindow::on_TEC_Enable_sel);
    connect(ui->TEC_PowerON_rB,  &QRadioButton::clicked, this, &MainWindow::on_TEC_Power_sel);
    connect(ui->TEC_PowerOFF_rB, &QRadioButton::clicked, this, &MainWindow::on_TEC_Power_sel);
    //自动积分（曝光）
    connect(ui->AE_ON_rB,  &QRadioButton::clicked, this, &MainWindow::on_AE_sel);
    connect(ui->AE_OFF_rB, &QRadioButton::clicked, this, &MainWindow::on_AE_sel);
    //探测器增益
    ui->SGAIN_ON_rB->setChecked(true);
    connect(ui->SGAIN_ON_rB,  &QRadioButton::clicked, this, &MainWindow::on_SGAIN_sel);
    connect(ui->SGAIN_OFF_rB, &QRadioButton::clicked, this, &MainWindow::on_SGAIN_sel);
    //两点校正
    connect(ui->NUC_ON_rB,  &QRadioButton::clicked, this, &MainWindow::on_FPGA_TwoPointCorrect_sel);
    connect(ui->NUC_O_rB,  &QRadioButton::clicked, this, &MainWindow::on_FPGA_TwoPointCorrect_sel);
    connect(ui->NUC_G_rB,  &QRadioButton::clicked, this, &MainWindow::on_FPGA_TwoPointCorrect_sel);
    connect(ui->NUC_OFF_rB, &QRadioButton::clicked, this, &MainWindow::on_FPGA_TwoPointCorrect_sel);
    //盲元去除
    connect(ui->BP_ON_rB,  &QRadioButton::clicked, this, &MainWindow::on_FPGA_BlindPointDetect_sel);
    connect(ui->BP_OFF_rB, &QRadioButton::clicked, this, &MainWindow::on_FPGA_BlindPointDetect_sel);
    //增强
    connect(ui->EH_ON_rB,  &QRadioButton::clicked, this, &MainWindow::on_FPGA_Enhance_sel);
    connect(ui->EH_OFF_rB, &QRadioButton::clicked, this, &MainWindow::on_FPGA_Enhance_sel);
    // Independent radio groups
    ui->MIR_On_rB->setAutoExclusive(false);
    ui->MIR_Off_rB->setAutoExclusive(false);
    ui->Crosshair_On_rB->setAutoExclusive(false);
    ui->Crosshair_Off_rB->setAutoExclusive(false);
    ui->Sharpness_On_rB->setAutoExclusive(false);
    ui->Sharpness_Off_rB->setAutoExclusive(false);

    auto *mirrorGroup = new QButtonGroup(this);
    mirrorGroup->setExclusive(true);
    mirrorGroup->addButton(ui->MIR_On_rB);
    mirrorGroup->addButton(ui->MIR_Off_rB);
    ui->MIR_On_rB->setChecked(true);

    auto *crosshairGroup = new QButtonGroup(this);
    crosshairGroup->setExclusive(true);
    crosshairGroup->addButton(ui->Crosshair_On_rB);
    crosshairGroup->addButton(ui->Crosshair_Off_rB);
    ui->Crosshair_Off_rB->setChecked(true);

    auto *sharpnessGroup = new QButtonGroup(this);
    sharpnessGroup->setExclusive(true);
    sharpnessGroup->addButton(ui->Sharpness_On_rB);
    sharpnessGroup->addButton(ui->Sharpness_Off_rB);
    ui->Sharpness_Off_rB->setChecked(true);
    // Crosshair and sharpness return
    connect(ui->Crosshair_On_rB, &QRadioButton::clicked, this, [this](bool checked){
        if (checked) emit serial_send_signal(QStringLiteral("EA0121FF0000200A"));
    });
    connect(ui->Crosshair_Off_rB, &QRadioButton::clicked, this, [this](bool checked){
        if (checked) emit serial_send_signal(QStringLiteral("EA0121F00000110A"));
    });
    connect(ui->Sharpness_On_rB, &QRadioButton::clicked, this, [this](bool checked){
        if (checked) emit serial_send_signal(QStringLiteral("EA0131FF0000300A"));
    });
    connect(ui->Sharpness_Off_rB, &QRadioButton::clicked, this, [this](bool checked){
        if (checked) emit serial_send_signal(QStringLiteral("EA0131F00000210A"));
    });
    connect(ui->chUpShift_tB, &QToolButton::clicked, this, [this]{
        emit serial_send_signal(QStringLiteral("EA0123FF0000220A"));
    });
    connect(ui->chDownShift_tB, &QToolButton::clicked, this, [this]{
        emit serial_send_signal(QStringLiteral("EA0123F00000130A"));
    });
    connect(ui->chLeftShift_tB, &QToolButton::clicked, this, [this]{
        emit serial_send_signal(QStringLiteral("EA0122F00000120A"));
    });
    connect(ui->chRightShift_tB, &QToolButton::clicked, this, [this]{
        emit serial_send_signal(QStringLiteral("EA0122FF0000210A"));
    });
    //LCD显示
    connect(serialworker,&SerialWorker::Int_LCDNumShow,this,&MainWindow::Int_LCDNumShow_slot);
    connect(serialworker,&SerialWorker::BoardTemp_LCDNumShow,this,&MainWindow::BoardTemp_LCDNumShow_slot);
    connect(serialworker,&SerialWorker::TECTemp_LCDNumShow,this,&MainWindow::TECTemp_LCDNumShow_slot);
    connect(serialworker,&SerialWorker::Sharpness_LCDNumShow,this,&MainWindow::Sharpness_LCDNumShow_slot);

    // 独立非模态串口调试窗口：串口线程负责解析，主线程只更新界面。
    m_telemetryDialog = new TelemetryDebugDialog(this);
    m_telemetryDialog->hide();
    connect(m_telemetryDialog, &TelemetryDebugDialog::commandRequested,
            serialworker, &SerialWorker::SerialSendBytes_Slot, Qt::QueuedConnection);
    connect(serialworker, &SerialWorker::telemetryFramesReady, this,
            [this](const QList<TelemetryFrame> &frames, int checksumErrors) {
        if (!m_telemetryDialog) return;
        m_telemetryDialog->addChecksumErrors(checksumErrors);
        for (const TelemetryFrame &frame : frames)
            m_telemetryDialog->handleFrame(frame);
    });

    auto *debugMenu = menuBar()->addMenu(QStringLiteral("调试"));
    auto *telemetryAction = debugMenu->addAction(QStringLiteral("串口调试状态"));
    connect(telemetryAction, &QAction::triggered, this, [this]() {
        m_telemetryDialog->show();
        m_telemetryDialog->raise();
        m_telemetryDialog->activateWindow();
    });
}

void MainWindow::initImageProcessing() {
    imgProc = new ImageProcessor(this);
// ====== 1. 设置 DockWidget 基础属性 ======
    ui->dockBpm->setWindowTitle(QStringLiteral("软件图像处理"));
    ui->dockBpm->setObjectName(QStringLiteral("dockImageProcessing"));
    addDockWidget(Qt::RightDockWidgetArea, ui->dockBpm);

    // ====== 2. 创建主容器和布局 ======
    QWidget *container = new QWidget(ui->dockBpm);
    QVBoxLayout *mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(10);

    // ====== 3. 图像处理部分 ======
    // 3.1 两点校正（NUC）分组
    grpNUC = new QGroupBox(QStringLiteral("两点校正（NUC）"), container);
    QVBoxLayout *vboxNUC = new QVBoxLayout(grpNUC);

    btnLowRef     = new QPushButton(QStringLiteral("采集暗场参考"), grpNUC);
    btnHighRef    = new QPushButton(QStringLiteral("采集亮场参考"), grpNUC);
    btnClearCalib = new QPushButton(QStringLiteral("清空校准数据"), grpNUC);
    rbNUC_On  = new QRadioButton(QStringLiteral("开启"), grpNUC);
    rbNUC_Off = new QRadioButton(QStringLiteral("关闭"), grpNUC);
    rbNUC_Off->setChecked(true);

    QHBoxLayout *hboxButtons = new QHBoxLayout();
    hboxButtons->addWidget(btnLowRef);
    hboxButtons->addWidget(btnHighRef);
    hboxButtons->addWidget(btnClearCalib);
    hboxButtons->addStretch(1);

    QHBoxLayout *hboxRadio = new QHBoxLayout();
    hboxRadio->addWidget(rbNUC_On);
    hboxRadio->addWidget(rbNUC_Off);
    hboxRadio->addStretch(1);

    vboxNUC->addLayout(hboxButtons);
    vboxNUC->addLayout(hboxRadio);
    mainLayout->addWidget(grpNUC);

    // ====== 3.2 最暗场补偿校正分组 ======
    grpDarkestOffset = new QGroupBox(QStringLiteral("最暗场补偿校正"), container);
    QVBoxLayout *vboxDarkestOffset = new QVBoxLayout(grpDarkestOffset);

    btnDarkestRef = new QPushButton(QStringLiteral("采集最暗场参考"), grpDarkestOffset);
    rbDarkestOffset_On  = new QRadioButton(QStringLiteral("开启"), grpDarkestOffset);
    rbDarkestOffset_Off = new QRadioButton(QStringLiteral("关闭"), grpDarkestOffset);
    rbDarkestOffset_Off->setChecked(true);

    QHBoxLayout *hboxDarkestButtons = new QHBoxLayout();
    hboxDarkestButtons->addWidget(btnDarkestRef);
    hboxDarkestButtons->addStretch(1);

    QHBoxLayout *hboxDarkestRadio = new QHBoxLayout();
    hboxDarkestRadio->addWidget(rbDarkestOffset_On);
    hboxDarkestRadio->addWidget(rbDarkestOffset_Off);
    hboxDarkestRadio->addStretch(1);

    vboxDarkestOffset->addLayout(hboxDarkestButtons);
    vboxDarkestOffset->addLayout(hboxDarkestRadio);
    mainLayout->addWidget(grpDarkestOffset);

    // ====== 4. BPM 参数部分（BPM 组移动到 NUC 之后，并新增“保存/加载 BPM”） ======
    QGroupBox *grpBPM = new QGroupBox(QStringLiteral("盲元去除"), container);
    QVBoxLayout *vboxBPM = new QVBoxLayout(grpBPM);

    // 4.1 盲元去除开关（新增）
    QHBoxLayout *hboxBlind = new QHBoxLayout();
    rbBlindOn  = new QRadioButton(QStringLiteral("开启"), grpBPM);
    rbBlindOff = new QRadioButton(QStringLiteral("关闭"), grpBPM);
    rbBlindOff->setChecked(true);
    hboxBlind->addWidget(rbBlindOn);
    hboxBlind->addWidget(rbBlindOff);
    hboxBlind->addStretch(1);
    vboxBPM->addLayout(hboxBlind);

    // 4.2 参数表单
    QFormLayout *formBPM = new QFormLayout();
    formBPM->setLabelAlignment(Qt::AlignLeft);

    sbGainMin  = new QDoubleSpinBox(grpBPM);
    sbGainMax  = new QDoubleSpinBox(grpBPM);
    sbDsnuAbs  = new QSpinBox(grpBPM);
    sbDsnuKmad = new QDoubleSpinBox(grpBPM);
    sbLinResid = new QSpinBox(grpBPM);
    sbBlack    = new QSpinBox(grpBPM);
    sbWhite    = new QSpinBox(grpBPM);
    chkAutoRebuild = new QCheckBox(QStringLiteral("自动重建 BPM"), grpBPM);
    auto *btnRebuild = new QPushButton(QStringLiteral("立即重建 BPM"), grpBPM);

    sbGainMin->setRange(0.10, 2.00); sbGainMin->setDecimals(2); sbGainMin->setSingleStep(0.01); sbGainMin->setValue(0.5);
    sbGainMax->setRange(0.10, 2.00); sbGainMax->setDecimals(2); sbGainMax->setSingleStep(0.01); sbGainMax->setValue(1.5);
    sbDsnuAbs->setRange(0, 5000);    sbDsnuAbs->setValue(120);
    sbDsnuKmad->setRange(0.0, 10.0); sbDsnuKmad->setDecimals(2); sbDsnuKmad->setSingleStep(0.1); sbDsnuKmad->setValue(3.0);
    sbLinResid->setRange(0, 5000);   sbLinResid->setValue(1300);
    sbBlack->setRange(0, 1000);      sbBlack->setValue(64);
    sbWhite->setRange(0, 1000);      sbWhite->setValue(64);

    formBPM->addRow(QStringLiteral("增益下限 (×中位数)"), sbGainMin);
    formBPM->addRow(QStringLiteral("增益上限 (×中位数)"), sbGainMax);
    formBPM->addRow(QStringLiteral("DSNU 绝对阈值 (DN)"), sbDsnuAbs);
    formBPM->addRow(QStringLiteral("DSNU k·MAD"),        sbDsnuKmad);
    formBPM->addRow(QStringLiteral("线性残差阈值 (DN)"), sbLinResid);
    formBPM->addRow(QStringLiteral("死黑阈值 (DN)"),     sbBlack);
    formBPM->addRow(QStringLiteral("死白阈值 (DN)"),     sbWhite);
    formBPM->addRow(chkAutoRebuild);
    formBPM->addRow(btnRebuild);
    vboxBPM->addLayout(formBPM);

    // 4.3 保存/加载 BPM（新增）
    QHBoxLayout *hboxBpmIO = new QHBoxLayout();
    auto *btnSaveBpm = new QPushButton(QStringLiteral("保存 BPM…"), grpBPM);
    auto *btnLoadBpm = new QPushButton(QStringLiteral("加载 BPM…"), grpBPM);
    hboxBpmIO->addWidget(btnSaveBpm);
    hboxBpmIO->addWidget(btnLoadBpm);
    hboxBpmIO->addStretch(1);
    vboxBPM->addLayout(hboxBpmIO);

    mainLayout->addWidget(grpBPM);

    // ====== 5. 中值滤波分组 ======
    grpMedian = new QGroupBox(QStringLiteral("中值滤波"), container);
    {
        QHBoxLayout *hboxMedian = new QHBoxLayout(grpMedian);
        rbMedian_On  = new QRadioButton(QStringLiteral("开启"), grpMedian);
        rbMedian_Off = new QRadioButton(QStringLiteral("关闭"), grpMedian);
        rbMedian_Off->setChecked(true);
        hboxMedian->addWidget(rbMedian_On);
        hboxMedian->addWidget(rbMedian_Off);
        hboxMedian->addStretch(1);
    }
    mainLayout->addWidget(grpMedian);

    // ====== 6. 直方图均衡分组 ======
    grpHistEq = new QGroupBox(QStringLiteral("直方图均衡"), container);
    {
        QVBoxLayout *vboxHistEq = new QVBoxLayout(grpHistEq);
        QHBoxLayout *hboxHistEq = new QHBoxLayout();
        rbHistEq_On  = new QRadioButton(QStringLiteral("开启"), grpHistEq);
        rbHistEq_Off = new QRadioButton(QStringLiteral("关闭"), grpHistEq);
        rbHistEq_Off->setChecked(true);
        hboxHistEq->addWidget(rbHistEq_On);
        hboxHistEq->addWidget(rbHistEq_Off);
        hboxHistEq->addStretch(1);
        vboxHistEq->addLayout(hboxHistEq);

        auto *formHistEq = new QFormLayout();
        formHistEq->setLabelAlignment(Qt::AlignLeft);

        sldHistUpper = new QSlider(Qt::Horizontal, grpHistEq);
        sldHistLower = new QSlider(Qt::Horizontal, grpHistEq);
        lblHistUpperValue = new QLabel(grpHistEq);
        lblHistLowerValue = new QLabel(grpHistEq);
        chkHistDownsample = new QCheckBox(QStringLiteral("4x4 下采样统计"), grpHistEq);

        sldHistUpper->setRange(1, 10000);
        sldHistUpper->setSingleStep(1);
        sldHistUpper->setPageStep(10);
        sldHistUpper->setValue(75);
        sldHistLower->setRange(0, 9999);
        sldHistLower->setSingleStep(1);
        sldHistLower->setPageStep(10);
        sldHistLower->setValue(15);

        auto *upperBox = new QWidget(grpHistEq);
        auto *upperLayout = new QHBoxLayout(upperBox);
        upperLayout->setContentsMargins(0, 0, 0, 0);
        upperLayout->addWidget(sldHistUpper, 1);
        upperLayout->addWidget(lblHistUpperValue);

        auto *lowerBox = new QWidget(grpHistEq);
        auto *lowerLayout = new QHBoxLayout(lowerBox);
        lowerLayout->setContentsMargins(0, 0, 0, 0);
        lowerLayout->addWidget(sldHistLower, 1);
        lowerLayout->addWidget(lblHistLowerValue);

        formHistEq->addRow(QStringLiteral("上平台阈值"), upperBox);
        formHistEq->addRow(QStringLiteral("下平台阈值"), lowerBox);
        formHistEq->addRow(chkHistDownsample);
        vboxHistEq->addLayout(formHistEq);
    }
    mainLayout->addWidget(grpHistEq);

    // ====== 7. 信号槽连接 ======
    // —— 捕获状态显示 ——
    connect(imgProc, &ImageProcessor::captureStatus,
            this, [&](const QString &msg){ ui->statusbar->showMessage(msg, 3000); });
    // 处理器 -> UI 重绘
//    connect(imgProc,&ImageProcessor::updataimage,this->m_imageWidget,&widget_image::repaintImage);//draw线程绘图结束，主线程更新界面
    connect(imgProc, &ImageProcessor::updataimage, this->m_glView,&GLImageWidget::repaintFromSharedImage);
    // 7.1 两点校正
    connect(rbNUC_On,  &QRadioButton::clicked, this, &MainWindow::on_TwoPointCorrect_sel);
    connect(rbNUC_Off, &QRadioButton::clicked, this, &MainWindow::on_TwoPointCorrect_sel);
    connect(btnLowRef,     &QPushButton::clicked, imgProc, &ImageProcessor::startCaptureLow);
    connect(btnHighRef,    &QPushButton::clicked, imgProc, &ImageProcessor::startCaptureHigh);
    connect(btnClearCalib, &QPushButton::clicked, imgProc, &ImageProcessor::clearCalibration);
    connect(btnDarkestRef, &QPushButton::clicked, imgProc, &ImageProcessor::startCaptureDarkest);
    connect(rbDarkestOffset_On,  &QRadioButton::clicked, this, &MainWindow::on_DarkestOffsetCorrect_sel);
    connect(rbDarkestOffset_Off, &QRadioButton::clicked, this, &MainWindow::on_DarkestOffsetCorrect_sel);

    // 盲元去除
    connect(rbBlindOn,  &QRadioButton::clicked, this, &MainWindow::on_BlindPointDetect_sel);
    connect(rbBlindOff, &QRadioButton::clicked, this, &MainWindow::on_BlindPointDetect_sel);

    // 7.2 BPM 参数变化（按你原先策略：变更→按需重建）
    auto sendAll = [this](bool rebuild){
        if (!imgProc) return;
        imgProc->setBpmParams(
            sbGainMin->value(),
            sbGainMax->value(),
            sbDsnuAbs->value(),
            sbDsnuKmad->value(),
            sbLinResid->value(),
            sbBlack->value(),
            sbWhite->value(),
            rebuild
        );
    };
    connect(sbGainMin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=](double v){
        if (!imgProc) return; imgProc->setGainMin(v);
        if (chkAutoRebuild->isChecked()) imgProc->rebuildBPM();
    });
    connect(sbGainMax,  QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=](double v){
        if (!imgProc) return; imgProc->setGainMax(v);
        if (chkAutoRebuild->isChecked()) imgProc->rebuildBPM();
    });
    connect(sbDsnuAbs,  QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int v){
        if (!imgProc) return; imgProc->setDsnuAbs(v);
        if (chkAutoRebuild->isChecked()) imgProc->rebuildBPM();
    });
    connect(sbDsnuKmad, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=](double v){
        if (!imgProc) return; imgProc->setDsnuKmad(v);
        if (chkAutoRebuild->isChecked()) imgProc->rebuildBPM();
    });
    connect(sbLinResid, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int v){
        if (!imgProc) return; imgProc->setLinResidualDN(v);
        if (chkAutoRebuild->isChecked()) imgProc->rebuildBPM();
    });
    connect(sbBlack,    QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int v){
        if (!imgProc) return; imgProc->setBlackThresh(v);
        if (chkAutoRebuild->isChecked()) imgProc->rebuildBPM();
    });
    connect(sbWhite,    QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int v){
        if (!imgProc) return; imgProc->setWhiteThresh(v);
        if (chkAutoRebuild->isChecked()) imgProc->rebuildBPM();
    });
    connect(btnRebuild, &QPushButton::clicked, this, [=]{ sendAll(true); });

    // 7.3 保存 / 加载 BPM（新增）
    connect(btnSaveBpm, &QPushButton::clicked, this, [=]{
        QString defaultName = QDateTime::currentDateTime().toString("yyyyMMdd_hhmm");
        QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存 BPM"),
            "bpm_"+defaultName+".png",
            QStringLiteral("BPM 图像 (*.png *.bmp *.tif);;所有文件 (*)"));
        if (path.isEmpty() || !imgProc) return;
        const bool ok = imgProc->saveBPM(path);
        // ImageProcessor::saveBPM 内部已通过 captureStatus 发状态，这里不必重复提示
        if (!ok) statusBar()->showMessage(QStringLiteral("保存 BPM 失败"), 3000);
    });

    connect(btnLoadBpm, &QPushButton::clicked, this, [=]{
        QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("加载 BPM"),
            QStringLiteral("BPM 图像 (*.png *.bmp *.tif);;所有文件 (*)"));
        if (path.isEmpty() || !imgProc) return;
        const bool ok = imgProc->loadBPM(path);
        if (!ok) statusBar()->showMessage(QStringLiteral("加载 BPM 失败"), 3000);
        // 加载成功后无需 rebuild，掩膜已生效；若你希望立刻作用于下一帧，可在此触发一次刷新
    });

    // 7.4 中值滤波 / 直方图均衡
    connect(rbMedian_On,  &QRadioButton::clicked, this, &MainWindow::on_medianblur_radio_sel);
    connect(rbMedian_Off, &QRadioButton::clicked, this, &MainWindow::on_medianblur_radio_sel);
    connect(rbHistEq_On,  &QRadioButton::clicked, this, &MainWindow::on_EqualizeHist_sel);
    connect(rbHistEq_Off, &QRadioButton::clicked, this, &MainWindow::on_EqualizeHist_sel);
    auto applyHistEqParams = [this]{
        if (!imgProc || !sldHistUpper || !sldHistLower) return;
        int upper = sldHistUpper->value();
        int lower = sldHistLower->value();
        if (upper <= lower) {
            upper = lower + 1;
            sldHistUpper->blockSignals(true);
            sldHistUpper->setValue(upper);
            sldHistUpper->blockSignals(false);
        }
        if (lblHistUpperValue) lblHistUpperValue->setText(QString::number(upper));
        if (lblHistLowerValue) lblHistLowerValue->setText(QString::number(lower));
        imgProc->setEqualizeHistThresholds(upper, lower);
    };
    connect(sldHistUpper, &QSlider::valueChanged, this, [=](int value){
        if (value <= sldHistLower->value()) {
            sldHistLower->blockSignals(true);
            sldHistLower->setValue(value - 1);
            sldHistLower->blockSignals(false);
        }
        applyHistEqParams();
    });
    connect(sldHistLower, &QSlider::valueChanged, this, [=](int value){
        if (value >= sldHistUpper->value()) {
            sldHistUpper->blockSignals(true);
            sldHistUpper->setValue(value + 1);
            sldHistUpper->blockSignals(false);
        }
        applyHistEqParams();
    });
    connect(chkHistDownsample, &QCheckBox::toggled, imgProc, &ImageProcessor::enableEqualizeHistDownsample);
    applyHistEqParams();

    // ====== 8. 设置 DockWidget 内容 ======
    container->setLayout(mainLayout);
    ui->dockBpm->setWidget(container);
    //帧率信号
//    connect(imgProc, &ImageProcessor::sourceFpsChanged, this,
//            [this](double fps){ srcFpsLabel->setText(QString("Src %1 FPS").arg(fps,0,'f',1)); });

}

//图像分辨率及像素深度设置
void MainWindow::applySpec(){
    const QString t = ui->comboResolution->currentText();
    const auto parts = t.split('x');
    if (parts.size() != 2) return;

    int w = parts[0].toInt();
    int h = parts[1].toInt();

    // 判断像素深度
    bool is16 = (ui->comboFormat->currentText() == "Grayscale16");
    QImage::Format fmt = is16 ? QImage::Format_Grayscale16 : QImage::Format_Grayscale8;
    int bytesPerPixel = is16 ? 2 : 1;
    // 更新显示控件
//    if (m_imageWidget) {
//        m_imageWidget->setImageSpec(w, h, fmt);
//    }
    // ★ 同步重建共享 QImage（供采集/处理写入，GL 再从它上传）
    {
        QMutexLocker lk(&widget_image::s_imgMutex);
        widget_image::image = QImage(w, h, fmt);
        widget_image::image.fill(0);
        // ★ 原始 16U 缓冲（紧密存储，stride = w）
        widget_image::resizeRaw(w, h, w);
    }
    if (m_glView) {
        m_glView->setImageSpec(w, h, is16 ? 16 : 8);
    }
    // 更新采集线程参数
    for (int i = 4; i < CHANNELS_NUM; ++i) {
        if (xferThread[i]) {
            xferThread[i]->setFrameSpec(w, h, 16, bytesPerPixel); // header固定16
        }
    }
    if (imgProc) imgProc->setSourceSpec(w, h, bytesPerPixel);
    for (int i = 4; i < CHANNELS_NUM; ++i) {
        if (xferThread[i]) {
            connect(xferThread[i], &transferThread::specChanged,
                    imgProc,     &ImageProcessor::setSourceSpec,
                    Qt::QueuedConnection);
        }
    }

}


void MainWindow::on_pushButton_retrieve_clicked()
{
    int i, cnt;
    QStringList devs;

    comboxDevice->clear();

    cnt = usbVidPids.count();
    if (cnt == 0) {
        devs = RetrieveDevice(0x706D, 0x807C);
    } else {
        for (i = 0; i < cnt; i++) {
            devs.append(RetrieveDevice(usbVidPids.at(i).vid,
                                       usbVidPids.at(i).pid));
        }
    }

    cnt = devs.count();
    //qDebug()<<cnt;//插入后为0，再retrive为1，之后保持，拔出USB变为0
    if (cnt == 0) {
        goto END;
    }

    for (i = 0; i < cnt; i++) {
        comboxDevice->addItem(devs.at(i));
    }

    /* free already opened usb device */
    if (usbSkeleton) {
        if (usbSkeleton->isOpen()) {
            usbSkeleton->close();
        }
        delete usbSkeleton;
        usbSkeleton = NULL;
    }

END:
    for (int i = 0; i < 8; i++) {
        ckbox_eps[i].setChecked(false);
        ledit_eps[i].clear();
    }
    ui->pushButton_start->setEnabled(false);
//    ui->checkBox->setEnabled(false);
//    ui->checkBox_HS->setEnabled(false);
    clearSatus();
}
void MainWindow::xmlUnpack(QXmlStreamReader *xml, QString &head)
{
    QString name, attr, val;
    QString title, vid, pid;
    QXmlStreamReader::TokenType xmlType;
    bool ok = false;
    usbParam_t usbParam;

    while (!xml->atEnd()) {
        xmlType = xml->readNext();
        if (xmlType == QXmlStreamReader::EndElement) {
            name = xml->name().toString();
            if (name == head)
                return;
        } if (xmlType == QXmlStreamReader::StartElement) {
            name = xml->name().toString();
            if (name == "ENUM_VID_PID") {
                attr = xml->attributes().value("type").toString();

                vid.clear();
                pid.clear();
                for (int i = 0; i < 2; i++) {
                    xml->readNextStartElement();
                    title = xml->name().toString();
                    if (title == "VID")
                        vid = xml->readElementText();
                    else if (title == "PID")
                        pid = xml->readElementText();
                }

                if (vid.isEmpty() || pid.isEmpty())
                    QMessageBox::warning(this,
                                         "warning",
                                         QString("Settings.xml：%1[%2] Format illegal!").arg(name).arg(attr));

                vid = vid.remove(" ").remove("0x");
                pid = pid.remove(" ").remove("0x");

                usbParam.vid = vid.toUInt(&ok, 16);
                if (!ok)
                    QMessageBox::warning(this,
                                         "warning",
                                         QString("Settings.xml：%1[%2] VID Format illegal!").arg(name).arg(attr));

                usbParam.pid = pid.toUInt(&ok, 16);
                if (!ok)
                    QMessageBox::warning(this,
                                         "warning",
                                         QString("Settings.xml：%1[%2] PID Format illegal!").arg(name).arg(attr));

                usbVidPids.append(usbParam);

            }
        }
    }
}

void MainWindow::loadXml()
{
    QFile file("./Settings.xml");
    if (!file.open(QIODevice::ReadOnly))
        return;

    QString head;
    QXmlStreamReader tmp(file.readAll());

    /* check xml format */
    while (!tmp.atEnd()) {
        tmp.readNext();
        if (tmp.hasError()) {
            QMessageBox::warning(this, "warning", "\"./Settings.xml\" Format illegal!");
            return;
        }
    }

    file.seek(0);
    QXmlStreamReader xml(file.readAll());
    while (!xml.atEnd()) {
        xml.readNextStartElement();
        head = xml.name().toString();
        xmlUnpack(&xml, head);
    }
}
void MainWindow::timerMonitor()
{
    QStringList devs;
    int i, cnt;

    cnt = usbVidPids.count();
    if (cnt == 0) {
        devs = RetrieveDevice(0x706D, 0x807C);
    } else {
        for (i = 0; i < cnt; i++) {
            devs.append(RetrieveDevice(usbVidPids.at(i).vid,
                                       usbVidPids.at(i).pid));
        }
    }

    cnt = devs.count();
    for (i = 0; i < cnt; i++) {
        if (devs.at(i) == currentDevice)
            return;
    }

    comboxDevice->clear();

    speedFile.write(QString("%1: %2")
                    .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd[hh:mm:ss]"))
                    .arg("Device disconnected !").toLatin1());
    speedFile.flush();
    monitor.stop();
}

void MainWindow::clearSatus()
{
    //ui->label_lost->clear();
    //ui->label_cnt->clear();
    ui->label_rate->clear();
}
void MainWindow::on_pushButton_connect_clicked()
{
    QList <WINUSB_PIPE_INFORMATION_EX> epList;
    WINUSB_PIPE_INFORMATION_EX ep;
    QString  epDisInfo;

    currentDevice = comboxDevice->currentText();
    if (currentDevice.isEmpty()) {
        QMessageBox::warning(this, "warning", "No Device Found!");
        clearSatus();
        return;
    }
    if (usbSkeleton) {
        if (usbSkeleton->isOpen()) {

            usbSkeleton->close();
        }
        delete usbSkeleton;
    }
    usbSkeleton = new tihUSBDevice(currentDevice);
    if (!usbSkeleton->open()) {
        QMessageBox::warning(this, "warning", "Device Connect Failed!");
        delete usbSkeleton;
        usbSkeleton = NULL;
        clearSatus();
        return;
    }

    epList = usbSkeleton->endPoints();
    for (int i = 0, cnt = epList.count(); i < cnt; i++) {
        ep = epList.at(i);
        epDisInfo.clear();

        /* show endpoint type exclude control endpoint */
        switch (ep.PipeType) {
        case UsbdPipeTypeBulk:
            epDisInfo.append("BULK ");
            break;
        case UsbdPipeTypeInterrupt:
            epDisInfo.append(" INT ");
            break;
        case UsbdPipeTypeIsochronous:
            epDisInfo.append(" ISO ");
            break;
        default:
            continue;
            break;
        }

        if (ep.PipeId & 0x80)
            epDisInfo.append(" IN: ");
        else
            epDisInfo.append("OUT: ");

        /* show endpoint address */
        epDisInfo.append(QString("0x%1").arg(ep.PipeId, 2, 16, QChar('0')));

        /* show max packet size */
        epDisInfo.append("; MaxPacketSize = " + QString::number(ep.MaximumPacketSize));

        /* if EP is ISO: show maxPacketInterval */
        if (ep.PipeType == UsbdPipeTypeIsochronous) {
            if (ep.MaximumBytesPerInterval == 0 || ep.Interval == 0) {
                /* if information error; clear all EP that already listed */
                QMessageBox::warning(this,
                                     "ISO EP Information",
                                     "\"MaxNbytesPerInterval or Interval\" "
                                     "in ISO EP descriptor should not be empty!");
                for (int j = 0; j < i; j++)
                    ledit_eps[i].clear();

                return;
            }
            epDisInfo.append(QString("; MaxNbytes/Interval = %1/%2")
                             .arg(ep.MaximumBytesPerInterval)
                             .arg(ep.Interval));
        }

        if (i < 8)
            ledit_eps[i].setText(epDisInfo);
    }

    /* pushButton "start" enabled only when there is valible EndPoints */
    if (ledit_eps[0].text().length()) {
        ui->pushButton_start->setEnabled(true);
        //ui->checkBox->setEnabled(true);
        //ui->checkBox_HS->setEnabled(true);

        // add
        //ui->checkBoxInputData->setEnabled(true);
    }
}
void MainWindow::transferRate()
{
    uint64_t ms;
    uint64_t dataInterval, msInterval;
    double speed;
//    // 创建 QElapsedTimer 实例
//    QElapsedTimer timer;
//    // 启动定时器
//    timer.start();

    ms = QDateTime::currentMSecsSinceEpoch();
    if (1000 < (ms - s_ms[s_ms_idx])) {
        s_ms_idx = (++s_ms_idx) % AVERAGE_POLL_SIZE;
        s_ms[s_ms_idx] = ms;

        s_nbytes_idx = (++s_nbytes_idx) % AVERAGE_POLL_SIZE;
        s_nbytes[s_nbytes_idx] = g_transOk;

        msInterval = s_ms[s_ms_idx] - s_ms[(s_ms_idx + 1) % AVERAGE_POLL_SIZE];
        dataInterval = s_nbytes[s_nbytes_idx] - s_nbytes[(s_nbytes_idx + 1) % AVERAGE_POLL_SIZE];

        speed = ((double)dataInterval) / (1.024 * (double)msInterval);
        ui->label_rate->setText(QString::number(speed, 'f', 2));

        speedFile.write(QString("%1[%2]: %3\n")
                        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd[hh:mm:ss.zzz]"))
                        .arg(msInterval)
                        .arg(QString::number(speed, 'f', 2)).toLatin1());
        speedFile.flush();
    }
     //获取经过的时间（毫秒）
//                qint64 elapsedMicroseconds = timer.nsecsElapsed()/1000;      //纳秒
//                qDebug() << "usb传输线程所耗时间： " << elapsedMicroseconds << "microseconds.";
//    ui->label_cnt->setText("0x" + QString::number(g_transOk, 16).toUpper());
    //if (g_transErr)
    //    ui->label_lost->setText("0x" + QString::number(g_transErr, 16).toUpper());
}
void MainWindow::on_pushButton_start_clicked()
{

    uint8_t pipeID;
    USBD_PIPE_TYPE pipeType;
    //uint32_t nbytes, interval;
    bool ok;
    uint32_t dataType;
    uint32_t dataSize;
    int eps = 0;
    bool hs = false;

    if (ui->pushButton_start->text() == "STOP") {
        for (int i = 4; i < CHANNELS_NUM; i++) {
            if (xferThread[i]) {
                if (xferThread[i]->isRunning()) {
                    xferThread[i]->stop();
                }
                delete xferThread[i];
                xferThread[i] = NULL;
            }
        }

//        imgProc->stop();
//        imgProc->quit();
//        imgProc->wait();
//        imgProc->deleteLater();
        // 顺手把数据流保存关掉，防止文件一直开着
        transferThread::stopStreamSave();
        ui->DataStreamSavepB->setText(tr("保存数据流"));
        m_streamSaving = false;

        ui->pushButton_start->setText("Start");
        ui->pushButton_retrieve->setEnabled(true);
        ui->pushButton_connect->setEnabled(true);
        timer.stop();
        return;
    }
    /* get Data Pattern 输出模式*/
    dataType = transferThread::DATA_PATTERN_CONSTANT;
    /* get Data Size */
    dataSize = 1024 * 1024;   //320/1024/需要能整除1024
    // —— NUC 开关
    bool nucOn =  rbNUC_On->isChecked(); // 代码里 -2 是“开”
    imgProc->enableTwoPoint(nucOn);
    // --BPM开关
    bool bpmon =  rbBlindOn->isChecked(); // 代码里 -2 是“开”
    imgProc->enableBadPixelFix(bpmon);
    // —— 输出位深：从 comboFormat 读取（只控制显示位深）
    bool display16 = (ui->comboFormat->currentText() == "Grayscale16");
    imgProc->setOutputBits(display16 ? 16 : 8);
    // thread2 = new QThread(this);//子2
    // imgProc->moveToThread(thread2);
    /* create threads */
//    int i=4;
//    //改为默认接收四通道
//    /* get Pipe IDS (EndPoints address) */
//    pipeID = ledit_eps[i].text().mid(12, 2).toInt(&ok, 16);
//    pipeType = UsbdPipeTypeBulk;
//    xferThread = new transferThread(usbSkeleton, 0, hs);
//    //xferThread[i]->setStackSize(4096 * 1024);
//    xferThread->setUsbPipe(pipeID, pipeType);
//    xferThread->setDataPattern(dataType, dataSize);
    for (int i = 4; i < CHANNELS_NUM; i++) {

        /* get Pipe IDS (EndPoints address) */
        pipeID = ledit_eps[i].text().mid(12, 2).toInt(&ok, 16);
        if (!ok)
            continue;

        if (ledit_eps[i].text().left(4) == "BULK")
            pipeType = UsbdPipeTypeBulk;
        else if (ledit_eps[i].text().left(4) == " INT")
            pipeType = UsbdPipeTypeInterrupt;
        else if (ledit_eps[i].text().left(4) == " ISO")
            pipeType = UsbdPipeTypeIsochronous;
        else
            continue;

        xferThread[i] = new transferThread(usbSkeleton,0, hs);
        xferThread[i]->setStackSize(4096 * 1024);
        xferThread[i]->setUsbPipe(pipeID, pipeType);
        xferThread[i]->setDataPattern(dataType, dataSize);

    }

    //为自定义的子线程分配空间  指定父对象
    //thread1 = new QThread(this);//子线程1

    //把自定义模块添加到子线程
    //xferThread->moveToThread(thread1);

    //连接信号与槽
    //connect(thread1,SIGNAL(started()),xferThread,SLOT(working()));//线程1启动时，usb线程开始传输
    for (int i = 4; i < CHANNELS_NUM; ++i) {    //统一分辨率及像素深度
        if (xferThread[i]) {
            connect(xferThread[i], &transferThread::updatapic, imgProc, &ImageProcessor::recv_data);
            connect(xferThread[i], &transferThread::specChanged,imgProc,&ImageProcessor::setSourceSpec,Qt::QueuedConnection);
//            connect(xferThread[i], &transferThread::usbFpsChanged, this,[this](double fps){srcFpsLabel->setText(QString("USB %.1f FPS").arg(fps));});
        }
    }
    connect(this, &MainWindow::destroyed, this , [=]()
    {
        for (int i = 4; i < CHANNELS_NUM; i++) {
            if (xferThread[i]) {
                xferThread[i]->stop();
                while(xferThread[i]->isRunning());
                delete xferThread[i];
            }
        }
        imgProc->stop();
        imgProc->quit();
        imgProc->wait();
        imgProc->deleteLater();

    });//界面销毁时消除线程

    // 在 start xferThreads 之前，调用一次 applySpec() 下发规格
    applySpec();
    /* start thread */
    //thread2->start();
    //qDebug()<<"thread2 "<<QThread::currentThread();
    imgProc->start();
    //qDebug()<<"drawthread "<<QThread::currentThread();
    for (int i = 4; i < CHANNELS_NUM; i++) {
        if (xferThread[i]) {
            xferThread[i]->start();
            eps++;

        }
    }


    //bool end_flag=true;

    if (!eps)
        return;
    g_transOk = 0;
    g_transErr = 0;
    s_nbytes_idx = 0;
    memset(s_nbytes, 0, AVERAGE_POLL_SIZE * sizeof(uint64_t));
    //表示以s_nbytes为首地址的数组或结构体，后AVERAGE_POLL_SIZE * sizeof(uint64_t)个字符都初始化为0。
    s_ms_idx = 0;
    for (int i = 0; i < AVERAGE_POLL_SIZE; i++)
        s_ms[i] = QDateTime::currentMSecsSinceEpoch();
    clearSatus();

    ui->pushButton_retrieve->setEnabled(false);
    ui->pushButton_connect->setEnabled(false);
    ui->pushButton_start->setText("STOP");
    timer.start(100);
    monitor.start(1000);

}

void MainWindow::on_medianblur_radio_sel()
{
    // 直接看“开启”单选是否勾选
    if (!rbMedian_On || !rbMedian_Off) return;
    const bool enable = rbMedian_On->isChecked();
    imgProc->enableMedianFiltering(enable);
}

void MainWindow::on_EqualizeHist_sel()
{
    if (!rbHistEq_On || !rbHistEq_Off) return;
    const bool enable = rbHistEq_On->isChecked();
    imgProc->enableEqualizeHist(enable);
}

void MainWindow::on_TwoPointCorrect_sel()
{
    if (!imgProc || !rbNUC_On || !rbNUC_Off) return;
    const bool enable = rbNUC_On->isChecked();
    imgProc->enableTwoPoint(enable);
}

void MainWindow::on_DarkestOffsetCorrect_sel()
{
    if (!imgProc || !rbDarkestOffset_On || !rbDarkestOffset_Off) return;
    const bool enable = rbDarkestOffset_On->isChecked();
    imgProc->enableDarkestOffsetCorrection(enable);
}

void MainWindow::on_BlindPointDetect_sel()
{
    if (!imgProc || !rbBlindOn || !rbBlindOff) return;
    const bool enable = rbBlindOn->isChecked();
    imgProc->enableBadPixelFix(enable);
}

void MainWindow::on_TEC_Power_sel()
{
    if (!ui->TEC_PowerON_rB || !ui->TEC_PowerOFF_rB) return;
    const bool Power = ui->TEC_PowerON_rB->isChecked();
    QList<float>SetVals;
    if(Power==1)
        SetVals.append(0xAF);
    else if(Power==0)
        SetVals.append(0xA0);
    emit InstructSettings_signal(0x1F,SetVals);
}

void MainWindow::on_TEC_Enable_sel()
{
    if (!ui->TEC_EnableON_rB || !ui->TEC_EnableOFF_rB) return;
    QList<float>SetVals;
    const bool enable = ui->TEC_EnableON_rB->isChecked();
    if(enable==1)
        SetVals.append(0xFF);
    else if(enable==0)
        SetVals.append(0xF0);
    emit InstructSettings_signal(0x1F,SetVals);
}

void MainWindow::on_AE_sel()
{
    if (!ui->AE_ON_rB || !ui->AE_OFF_rB) return;
    QList<float>SetVals;
    const bool enable = ui->AE_ON_rB->isChecked();
    if(enable==1)
        SetVals.append(0xFF);
    else if(enable==0)
        SetVals.append(0xF0);
    emit InstructSettings_signal(0x20,SetVals);
}

void MainWindow::on_SGAIN_sel()
{
    if (!ui->SGAIN_ON_rB || !ui->SGAIN_OFF_rB) return;
    QList<float>SetVals;
    const bool enable = ui->SGAIN_ON_rB->isChecked();
    if(enable==1)
        SetVals.append(0xFF);
    else if(enable==0)
        SetVals.append(0xF0);
    emit InstructSettings_signal(0x2C,SetVals);
}

void MainWindow::on_FPGA_TwoPointCorrect_sel()
{
    if (!ui->NUC_ON_rB || !ui->NUC_G_rB|| !ui->NUC_O_rB|| !ui->NUC_OFF_rB) return;
    QList<float>SetVals;
    if(ui->NUC_ON_rB->isChecked())
        SetVals.append(0xFF);
    else if(ui->NUC_G_rB->isChecked())
        SetVals.append(0xEE);
    else if(ui->NUC_O_rB->isChecked())
        SetVals.append(0xE0);
    else if(ui->NUC_OFF_rB->isChecked())
        SetVals.append(0xF0);
    else{
        // 如果没有按钮被选中，记录警告
        qWarning() << "No NUC option selected!";
        SetVals.append(0x00);  // 默认或错误值
    }
    emit InstructSettings_signal(0x02,SetVals);
}

void MainWindow::on_FPGA_BlindPointDetect_sel()
{
    if (!ui->BP_ON_rB || !ui->BP_OFF_rB) return;
    QList<float>SetVals;
    const bool enable = ui->BP_ON_rB->isChecked();
    if(enable==1)
        SetVals.append(0xFF);
    else if(enable==0)
        SetVals.append(0xF0);
    emit InstructSettings_signal(0x03,SetVals);
}

void MainWindow::on_FPGA_Enhance_sel()
{
    if (!ui->EH_ON_rB || !ui->EH_OFF_rB) return;
    QList<float>SetVals;
    const bool enable = ui->EH_ON_rB->isChecked();
    if(enable==1)
        SetVals.append(0xFF);
    else if(enable==0)
        SetVals.append(0xF0);
    emit InstructSettings_signal(0x04,SetVals);
}


void MainWindow::on_FrameSavepB_clicked()
{
    b_frame_save=true;

    // 从共享图像里取当前规格（加锁，避免竞态）
    int w = 0, h = 0, bits = 8;
    {
        QMutexLocker lk(&widget_image::s_imgMutex);
        w = widget_image::image.width();
        h = widget_image::image.height();
        bits = (widget_image::image.format() == QImage::Format_Grayscale16) ? 16 : 8;
    }
    if (!imgProc) return;

    if (m_imageSaveDir.isEmpty()) m_imageSaveDir = QDir::currentPath();
    QDir().mkpath(m_imageSaveDir);

    const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    const QString defaultFileName = QString("frame_%1_%2x%3_%4bit.%5")
                                        .arg(ts).arg(w).arg(h).arg(bits).arg(m_imageSaveExt);

    // 若 savednameTE 为空，沿用时间戳默认名；否则使用用户输入名。
    QString fileName = ui->savednameTE->toPlainText().trimmed();
    if (fileName.isEmpty()) fileName = defaultFileName;

    // Windows 文件名非法字符清洗，避免保存失败。
    fileName.replace('\\', '_');
    fileName.replace('/', '_');
    fileName.replace(':', '_');
    fileName.replace('*', '_');
    fileName.replace('?', '_');
    fileName.replace('"', '_');
    fileName.replace('<', '_');
    fileName.replace('>', '_');
    fileName.replace('|', '_');

    if (!fileName.contains('.')) fileName += ("." + m_imageSaveExt);

    const QString path = QDir(m_imageSaveDir).filePath(fileName);

    const bool ok = imgProc->saveFrame(path);
    statusBar()->showMessage(ok ? QString("纯图像已保存：%1").arg(path)
                                : QString("纯图像保存失败：%1").arg(path), 3000);
}


void MainWindow::on_DataStreamSavepB_clicked()
{
    if (!m_streamSaving) {
        if (m_streamSaveDir.isEmpty()) m_streamSaveDir = QDir::currentPath();
        QDir().mkpath(m_streamSaveDir);

        const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        const QString path = QDir(m_streamSaveDir).filePath(QString("datastream_%1.raw").arg(ts));

        transferThread::startStreamSave(path);
        ui->DataStreamSavepB->setText(tr("停止保存"));
        m_streamSaving = true;
        statusBar()->showMessage(QString("开始保存数据流：%1").arg(path), 3000);
    } else {
        transferThread::stopStreamSave();
        ui->DataStreamSavepB->setText(tr("保存数据流"));
        m_streamSaving = false;
        statusBar()->showMessage(QStringLiteral("数据流保存已停止"), 3000);
    }
}

void MainWindow::on_serialpB_clicked()
{
    if(!serial_bind_flag)
    {
        emit open_serial_signal(ui->serialCb->currentText());
        QThread::msleep(10);
        if(!serial_bind_flag)
        {
            QMessageBox::critical(this,tr("Error"),"串口已被占用，请检查是否正确连接");
        }
        else
        {
            ui->serialpB->setText("关闭串口");
            ui->serialpB->setIcon(QIcon(":/icons/serial_open.png"));
            ui->serial_det_pB->setEnabled(false);
            ui->serialCb->setEnabled(false);
        }

    }
    else if(serial_bind_flag)    //将按键与串口是否连接分开来判断，防止无可用串口时按键变灰无法选中
    {
        emit close_serial_signal();
        ui->serialpB->setText("打开串口");
        ui->serialpB->setIcon(QIcon(":/icons/serial_close.png"));
        ui->serial_det_pB->setEnabled(true);
        ui->serialCb->setEnabled(true);
    }
}


void MainWindow::on_serial_det_pB_clicked()
{
    QString currentSerialPort  = ui->serialCb->currentText();
    ui->serialCb->clear();   //清空列表
    QStringList serialNamePort;
    for (const auto& it : QSerialPortInfo::availablePorts())
    {
        serialNamePort<<it.portName();

    }
    ui->serialCb->addItems(serialNamePort);
    // 检查是否存在之前选择的串口，如果存在则恢复选择
    if (serialNamePort.contains(currentSerialPort))
    {
        ui->serialCb->setCurrentText(currentSerialPort);
    }
}


void MainWindow::on_comv_pB_clicked()
{
    QList<float>comSetVals;
    comSetVals.append(ui->comv_dSB->value());
    emit InstructSettings_signal(0x06,comSetVals);
}


void MainWindow::on_Int_pB_clicked()
{
    if(ui->AE_ON_rB->isChecked()) return;    //如果是自动积分则无法设置
    QList<float>IntSetVals;
    IntSetVals.append(ui->Int_dsB->value());
    emit InstructSettings_signal(0x05,IntSetVals);
}


void MainWindow::on_TECSet_pB_clicked()
{
    QList<float>TECSetVals;
    TECSetVals.append(ui->TECV_dSB->value());
    emit InstructSettings_signal(0x0F,TECSetVals);
}

void MainWindow::Int_LCDNumShow_slot(float time)
{
    ui->IntShow_lcd->display(QString::number(time, 'f', 2));
}

void MainWindow::BoardTemp_LCDNumShow_slot(float temp)
{
    ui->TempBoardT_lcd->display(QString::number(temp, 'f', 3));
}

void MainWindow::TECTemp_LCDNumShow_slot(std::vector<float> temp)
{
    ui->ITEC_lcd->display(QString::number(temp[0], 'f', 3));
    ui->VTEC_lcd->display(QString::number(temp[1], 'f', 3));
    ui->TECTACT_lcd->display(QString::number(temp[2], 'f', 3));
    ui->TECTSET_lcd->display(QString::number(temp[3], 'f', 3));
}

void MainWindow::Sharpness_LCDNumShow_slot(int value)
{
    ui->SharpnessValue_lcd->display(QString::number(value));
}


void MainWindow::on_collect_brightfield_pB_clicked()
{
    QList<float>SetVals;
    SetVals.append(0xFF);
    emit InstructSettings_signal(0x01,SetVals);
}


void MainWindow::on_collect_darkfield_pB_clicked()
{
    QList<float>SetVals;
    SetVals.append(0xF0);
    emit InstructSettings_signal(0x01,SetVals);
}




void MainWindow::on_save_pathtB_clicked()
{
    // 1) 选择统一保存目录（图像与数据流共用）
    const QString imgDir = QFileDialog::getExistingDirectory(
        this,
        tr("选择保存目录（图像/数据流）"),
        m_imageSaveDir.isEmpty() ? QDir::currentPath() : m_imageSaveDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (imgDir.isEmpty()) return;

    // 2) 选择纯图像保存格式
    const QStringList fmtList = {"raw", "png", "tif", "bmp"};
    int idx = fmtList.indexOf(m_imageSaveExt);
    if (idx < 0) idx = 0;
    bool ok = false;
    const QString ext = QInputDialog::getItem(
        this,
        tr("选择纯图像保存格式"),
        tr("图像后缀"),
        fmtList,
        idx,
        false,
        &ok).toLower();
    if (!ok || ext.isEmpty()) return;

    m_imageSaveDir = imgDir;
    m_streamSaveDir = imgDir;
    m_imageSaveExt = ext;

    QSettings s("SWIRVision", "SWIRVision");
    s.setValue("save/imageDir",  m_imageSaveDir);
    s.setValue("save/imageExt",  m_imageSaveExt);

    ui->save_pathtB->setToolTip(
        QString("图像/数据流: %1\n图像格式: .%2")
            .arg(m_imageSaveDir, m_imageSaveExt));
    statusBar()->showMessage(
        QString("保存配置已更新：图像/数据流[%1]，图像格式[.%2]")
            .arg(m_imageSaveDir, m_imageSaveExt),
        5000);

}


void MainWindow::on_twoPointsFixPB_clicked()
{

    emit serial_send_signal(QStringLiteral("EA010CFF00000B0A")); // 固化两点
    statusBar()->showMessage(QStringLiteral("已发送：固化两点 (0x0C)"), 2000);
}


void MainWindow::on_configFixpB_clicked()
{

    emit serial_send_signal(QStringLiteral("EA010BFF00000A0A")); // 固化配置
    statusBar()->showMessage(QStringLiteral("已发送：固化配置 (0x0B)"), 2000);
}



void MainWindow::on_paramReadpB_clicked()
{

    emit serial_send_signal(QStringLiteral("EA010DFF00000C0A")); // 读取两点及配置
    statusBar()->showMessage(QStringLiteral("已发送：读取两点及配置 (0x0D)"), 2000);
}

