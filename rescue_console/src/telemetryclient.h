#pragma once

#include "protocol.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>

class TelemetryClient : public QObject {
    Q_OBJECT
public:
    explicit TelemetryClient(QObject *parent = nullptr);
    void connectToHost(const QString &host, quint16 port);
    void disconnectFromHost();
    QString sendControl(const QString &action, const QJsonObject &params = {});
    bool isConnected() const;
    qint64 elapsedSinceTelemetryMs() const;

signals:
    void telemetryReceived(const QJsonObject &message);
    void commandAcknowledged(const QJsonObject &message);
    void statusChanged(const QString &status);
    void connectedChanged(bool connected);
    void errorOccurred(const QString &message);
    void frameRejected(const QString &reason);
    void dataFreshnessChanged(bool fresh);
    void commandTimedOut(const QString &id);

private:
    void readIncoming();
    void updateTimeouts();
    QTcpSocket m_socket;
    QTimer m_watchdog;
    RescueProtocol::FrameDecoder m_decoder;
    QElapsedTimer m_sessionClock;
    QElapsedTimer m_lastTelemetry;
    QHash<QString, qint64> m_pending;
    bool m_fresh = false;
    bool m_staleReported = false;
    static constexpr qint64 StaleTimeoutMs = 3000;
};
