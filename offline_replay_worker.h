#ifndef OFFLINE_REPLAY_WORKER_H
#define OFFLINE_REPLAY_WORKER_H

#include <QByteArray>
#include <QSemaphore>
#include <QString>
#include <QThread>

#include <atomic>

class OfflineReplayWorker : public QThread
{
    Q_OBJECT
public:
    struct Settings {
        QString filePath;
        int width = 0;
        int height = 0;
        double framesPerSecond = 5.0;
        bool loop = false;
    };

    explicit OfflineReplayWorker(const Settings &settings, QObject *parent = nullptr);

    void requestStop();
    void acknowledgeFrame();

signals:
    void frameDecoded(const QByteArray &frameBytes, quint64 frameIndex);
    void replayError(const QString &message);
    void replayCompleted(quint64 decodedFrames, bool canceled);

protected:
    void run() override;

private:
    Settings m_settings;
    std::atomic_bool m_stopRequested{false};
    QSemaphore m_frameCredit{1};
};

#endif // OFFLINE_REPLAY_WORKER_H
