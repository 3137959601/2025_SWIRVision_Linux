//#include "widget.h"
#include "mainwindow.h"
#include "common/device.h"
#include "common/tih_usb_device.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QRegularExpression>
#include <QTimer>

namespace {

bool parseUsbId(const QString &text, std::uint16_t &vid, std::uint16_t &pid)
{
    static const QRegularExpression idPattern(
        QStringLiteral("^([0-9a-fA-F]{4}):([0-9a-fA-F]{4})$"));
    const auto match = idPattern.match(text);
    if (!match.hasMatch())
        return false;
    bool vidOk = false;
    bool pidOk = false;
    vid = std::uint16_t(match.captured(1).toUInt(&vidOk, 16));
    pid = std::uint16_t(match.captured(2).toUInt(&pidOk, 16));
    return vidOk && pidOk;
}

QString pipeTypeName(UsbPipeType type)
{
    switch (type) {
    case UsbPipeType::Control: return QStringLiteral("control");
    case UsbPipeType::Isochronous: return QStringLiteral("isochronous");
    case UsbPipeType::Bulk: return QStringLiteral("bulk");
    case UsbPipeType::Interrupt: return QStringLiteral("interrupt");
    case UsbPipeType::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("SWIRVision 400W Linux迁移版"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("offline"), QStringLiteral("回放16位无头RAW多帧文件"), QStringLiteral("file")});
    parser.addOption({QStringLiteral("offline-width"), QStringLiteral("离线RAW宽度"), QStringLiteral("pixels"), QStringLiteral("2048")});
    parser.addOption({QStringLiteral("offline-height"), QStringLiteral("离线RAW高度"), QStringLiteral("pixels"), QStringLiteral("2048")});
    parser.addOption({QStringLiteral("offline-fps"), QStringLiteral("离线回放帧率"), QStringLiteral("fps"), QStringLiteral("5")});
    parser.addOption({QStringLiteral("offline-loop"), QStringLiteral("循环回放离线RAW")});
    parser.addOption({QStringLiteral("offline-frames"), QStringLiteral("处理指定帧数后退出（0表示不自动退出）"), QStringLiteral("count"), QStringLiteral("0")});
    parser.addOption({QStringLiteral("offline-save"), QStringLiteral("自动退出前保存当前处理帧"), QStringLiteral("file")});
    parser.addOption({QStringLiteral("usb-list"), QStringLiteral("只读枚举指定VID:PID后退出"), QStringLiteral("vid:pid")});
    parser.addOption({QStringLiteral("usb-open-check"), QStringLiteral("打开并声明指定VID:PID设备，打印端点后关闭"), QStringLiteral("vid:pid")});
    parser.process(a);

    const QString usbList = parser.value(QStringLiteral("usb-list"));
    const QString usbOpenCheck = parser.value(QStringLiteral("usb-open-check"));
    if (!usbList.isEmpty() && !usbOpenCheck.isEmpty()) {
        qCritical() << "USB_ERROR: --usb-list和--usb-open-check不能同时使用";
        return 2;
    }
    if (!usbList.isEmpty() || !usbOpenCheck.isEmpty()) {
        const QString usbId = usbList.isEmpty() ? usbOpenCheck : usbList;
        std::uint16_t vid = 0;
        std::uint16_t pid = 0;
        if (!parseUsbId(usbId, vid, pid)) {
            qCritical().noquote()
                << "USB_ERROR: 参数必须是4位十六进制VID:PID，例如706d:807c";
            return 2;
        }
        const QStringList devices = RetrieveDevice(vid, pid);
        if (!usbList.isEmpty()) {
            for (const QString &device : devices)
                qInfo().noquote() << "USB_LIST_DEVICE" << device;
            qInfo().noquote() << QStringLiteral("USB_LIST_COUNT vid=%1 pid=%2 count=%3")
                                     .arg(vid, 4, 16, QLatin1Char('0'))
                                     .arg(pid, 4, 16, QLatin1Char('0'))
                                     .arg(devices.size());
            return 0;
        }

        if (devices.isEmpty()) {
            qCritical().noquote() << QStringLiteral("USB_OPEN_ERROR: 未找到设备 vid=%1 pid=%2")
                                         .arg(vid, 4, 16, QLatin1Char('0'))
                                         .arg(pid, 4, 16, QLatin1Char('0'));
            return 3;
        }

        tihUSBDevice device(devices.first());
        if (!device.open()) {
            qCritical().noquote() << "USB_OPEN_ERROR:" << device.lastError();
            return 4;
        }
        const QList<UsbEndpointInfo> endpoints = device.endPoints();
        int bulkInCount = 0;
        int bulkOutCount = 0;
        for (const UsbEndpointInfo &endpoint : endpoints) {
            if (endpoint.pipeType == UsbPipeType::Bulk) {
                if ((endpoint.address & 0x80U) != 0)
                    ++bulkInCount;
                else
                    ++bulkOutCount;
            }
            qInfo().noquote()
                << QStringLiteral("USB_OPEN_ENDPOINT address=0x%1 type=%2 max_packet=%3 interval=%4 max_bytes=%5")
                       .arg(endpoint.address, 2, 16, QLatin1Char('0'))
                       .arg(pipeTypeName(endpoint.pipeType))
                       .arg(endpoint.maximumPacketSize)
                       .arg(endpoint.interval)
                       .arg(endpoint.maximumBytesPerInterval);
        }
        qInfo().noquote()
            << QStringLiteral("USB_OPEN_OK device=%1 endpoints=%2 bulk_in=%3 bulk_out=%4")
                   .arg(devices.first())
                   .arg(endpoints.size())
                   .arg(bulkInCount)
                   .arg(bulkOutCount);
        device.close();
        qInfo() << "USB_CLOSE_OK";
        return 0;
    }

    MainWindow w;
    w.show();

    const QString offlineFile = parser.value(QStringLiteral("offline"));
    if (!offlineFile.isEmpty()) {
        bool widthOk = false, heightOk = false, fpsOk = false, countOk = false;
        const int width = parser.value(QStringLiteral("offline-width")).toInt(&widthOk);
        const int height = parser.value(QStringLiteral("offline-height")).toInt(&heightOk);
        const double fps = parser.value(QStringLiteral("offline-fps")).toDouble(&fpsOk);
        const int targetFrames = parser.value(QStringLiteral("offline-frames")).toInt(&countOk);
        const QString savePath = parser.value(QStringLiteral("offline-save"));
        if (!widthOk || !heightOk || !fpsOk || !countOk || targetFrames < 0) {
            qCritical() << "OFFLINE_TEST_ERROR: 命令行宽、高、FPS或帧数参数无效";
            return 2;
        }

        struct OfflineCliState {
            int processedCount = 0;
            bool failed = false;
        };
        auto *state = new OfflineCliState;
        QObject::connect(&w, &MainWindow::offlineReplayFailed, &a,
                         [&a, state](const QString &) {
            state->failed = true;
            a.exit(3);
        });
        QObject::connect(&w, &MainWindow::offlineFrameProcessed, &a,
                         [&a, &w, state, targetFrames, savePath](quint64 frameIndex) {
            ++state->processedCount;
            qInfo().noquote() << "OFFLINE_TEST_FRAME index=" << frameIndex
                              << "processed=" << state->processedCount;
            if (targetFrames <= 0 || state->processedCount < targetFrames)
                return;
            if (!savePath.isEmpty() && !w.saveCurrentFrame(savePath)) {
                state->failed = true;
                qCritical().noquote() << "OFFLINE_TEST_ERROR: 保存处理帧失败：" << savePath;
                w.stopOfflineReplay();
                a.exit(4);
                return;
            }
            if (!savePath.isEmpty())
                qInfo().noquote() << "OFFLINE_TEST_SAVE_OK file=" << savePath;
            w.stopOfflineReplay();
            QTimer::singleShot(100, &a, [&a]() { a.exit(0); });
        });
        QObject::connect(&w, &MainWindow::offlineReplayEnded, &a,
                         [&a, state, targetFrames](quint64, bool canceled, bool success) {
            if (success && !state->failed && !canceled && targetFrames > 0 &&
                state->processedCount < targetFrames) {
                qCritical() << "OFFLINE_TEST_ERROR: 文件结束前未达到目标处理帧数";
                a.exit(5);
            }
        });
        QObject::connect(&a, &QCoreApplication::aboutToQuit, &a,
                         [state]() { delete state; });

        QTimer::singleShot(0, &w, [&w, &a, offlineFile, width, height, fps, &parser]() {
            if (!w.startOfflineReplayFile(offlineFile, width, height, fps,
                                          parser.isSet(QStringLiteral("offline-loop")),
                                          16, false)) {
                a.exit(2);
            }
        });
    }
    return a.exec();
}
