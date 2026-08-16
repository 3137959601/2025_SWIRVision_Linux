#include "offline_replay_worker.h"

#include <QFile>
#include <QtGlobal>

#include <limits>

OfflineReplayWorker::OfflineReplayWorker(const Settings &settings, QObject *parent)
    : QThread(parent), m_settings(settings)
{
}

void OfflineReplayWorker::requestStop()
{
    m_stopRequested.store(true, std::memory_order_release);
    m_frameCredit.release();
}

void OfflineReplayWorker::acknowledgeFrame()
{
    m_frameCredit.release();
}

void OfflineReplayWorker::run()
{
    const qint64 pixelCount = qint64(m_settings.width) * qint64(m_settings.height);
    if (m_settings.width <= 0 || m_settings.height <= 0 ||
        pixelCount > std::numeric_limits<int>::max() / qint64(sizeof(quint16))) {
        emit replayError(QStringLiteral("离线回放尺寸无效：%1x%2")
                             .arg(m_settings.width)
                             .arg(m_settings.height));
        emit replayCompleted(0, false);
        return;
    }
    if (!(m_settings.framesPerSecond > 0.0) || m_settings.framesPerSecond > 1000.0) {
        emit replayError(QStringLiteral("离线回放帧率必须大于0且不超过1000 FPS"));
        emit replayCompleted(0, false);
        return;
    }

    const qint64 frameBytes = pixelCount * qint64(sizeof(quint16));
    QFile file(m_settings.filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit replayError(QStringLiteral("无法打开离线RAW文件：%1；原因：%2")
                             .arg(m_settings.filePath, file.errorString()));
        emit replayCompleted(0, false);
        return;
    }
    if (file.size() < frameBytes) {
        emit replayError(QStringLiteral("离线RAW文件不足一帧：文件%1字节，单帧需要%2字节")
                             .arg(file.size())
                             .arg(frameBytes));
        emit replayCompleted(0, false);
        return;
    }
    if ((file.size() % frameBytes) != 0) {
        emit replayError(QStringLiteral("离线RAW文件尾部不是完整帧：文件%1字节，单帧%2字节，余数%3字节")
                             .arg(file.size())
                             .arg(frameBytes)
                             .arg(file.size() % frameBytes));
        emit replayCompleted(0, false);
        return;
    }

    const unsigned long intervalMs = qMax(
        1UL, static_cast<unsigned long>(qRound(1000.0 / m_settings.framesPerSecond)));
    quint64 decodedFrames = 0;

    while (!m_stopRequested.load(std::memory_order_acquire)) {
        if (!m_frameCredit.tryAcquire(1, 100))
            continue;
        if (m_stopRequested.load(std::memory_order_acquire))
            break;

        if (file.atEnd()) {
            if (!m_settings.loop) {
                emit replayCompleted(decodedFrames, false);
                return;
            }
            if (!file.seek(0)) {
                emit replayError(QStringLiteral("离线RAW循环回放无法回到文件开头：%1")
                                     .arg(file.errorString()));
                emit replayCompleted(decodedFrames, false);
                return;
            }
        }

        QByteArray frame = file.read(frameBytes);
        if (frame.size() != frameBytes) {
            emit replayError(QStringLiteral("离线RAW读取到不完整帧：期望%1字节，实际%2字节")
                                 .arg(frameBytes)
                                 .arg(frame.size()));
            emit replayCompleted(decodedFrames, false);
            return;
        }

        emit frameDecoded(frame, decodedFrames);
        ++decodedFrames;
        QThread::msleep(intervalMs);
    }

    emit replayCompleted(decodedFrames, true);
}
