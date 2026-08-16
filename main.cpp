//#include "widget.h"
#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

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
    parser.process(a);

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

        auto *processedCount = new int(0);
        QObject::connect(&w, &MainWindow::offlineReplayFailed, &a,
                         [&a](const QString &) { a.exit(3); });
        QObject::connect(&w, &MainWindow::offlineFrameProcessed, &a,
                         [&a, &w, processedCount, targetFrames, savePath](quint64 frameIndex) {
            ++(*processedCount);
            qInfo().noquote() << "OFFLINE_TEST_FRAME index=" << frameIndex
                              << "processed=" << *processedCount;
            if (targetFrames <= 0 || *processedCount < targetFrames)
                return;
            if (!savePath.isEmpty() && !w.saveCurrentFrame(savePath)) {
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
                         [&a, processedCount, targetFrames](quint64, bool canceled) {
            if (!canceled && targetFrames > 0 && *processedCount < targetFrames) {
                qCritical() << "OFFLINE_TEST_ERROR: 文件结束前未达到目标处理帧数";
                a.exit(5);
            }
        });
        QObject::connect(&a, &QCoreApplication::aboutToQuit, &a,
                         [processedCount]() { delete processedCount; });

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
