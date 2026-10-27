#pragma once

#include <QImage>
#include <QThread>

// Capture ownership stays inside run(). Stop asynchronously and destroy this
// object only after QThread::finished; USB drivers may ignore read timeouts.
class VideoWorker final : public QThread
{
    Q_OBJECT

public:
    explicit VideoWorker(QString source, QObject* parent = nullptr);
    void requestStop();

signals:
    void frameReady(QImage frame);
    void statusChanged(QString message);
    void streamInfo(QString information);

protected:
    void run() override;

private:
    bool sleepUntil(qint64 deadlineMs, const class QElapsedTimer& clock);
    QString m_source;
};
