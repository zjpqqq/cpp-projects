#include "telemetryclient.h"

#include <QUuid>

TelemetryClient::TelemetryClient(QObject *parent) : QObject(parent)
{
    m_socket.setReadBufferSize(RescueProtocol::MaxPayloadBytes + 4);
    m_watchdog.setInterval(250);
    connect(&m_watchdog, &QTimer::timeout, this, &TelemetryClient::updateTimeouts);
    connect(&m_socket, &QTcpSocket::readyRead, this, &TelemetryClient::readIncoming);
    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        m_sessionClock.start();
        m_lastTelemetry.invalidate();
        m_staleReported = false;
        m_watchdog.start();
        emit connectedChanged(true);
        emit statusChanged(QStringLiteral("TCP 已连接，等待遥测"));
    });
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        m_watchdog.stop();
        m_decoder.clear();
        m_lastTelemetry.invalidate();
        m_sessionClock.invalidate();
        const auto abandoned = m_pending.keys();
        m_pending.clear();
        for (const QString &id : abandoned)
            emit commandTimedOut(id);
        m_fresh = false;
        emit dataFreshnessChanged(false);
        emit connectedChanged(false);
        emit statusChanged(QStringLiteral("TCP 未连接"));
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
        emit errorOccurred(m_socket.errorString());
        emit statusChanged(QStringLiteral("连接错误：%1").arg(m_socket.errorString()));
    });
}

void TelemetryClient::connectToHost(const QString &host, quint16 port)
{
    m_socket.abort();
    m_decoder.clear();
    m_lastTelemetry.invalidate();
    m_fresh = false;
    m_staleReported = false;
    m_pending.clear();
    if (host.trimmed().isEmpty() || port == 0) {
        emit errorOccurred(QStringLiteral("请填写主机地址与有效端口"));
        return;
    }
    emit statusChanged(QStringLiteral("正在连接 %1:%2 …").arg(host).arg(port));
    m_socket.connectToHost(host.trimmed(), port);
}

void TelemetryClient::disconnectFromHost()
{
    // Abort makes an explicit user disconnect immediate, including DNS lookup.
    m_socket.abort();
    m_decoder.clear();
    emit statusChanged(QStringLiteral("TCP 未连接"));
}

bool TelemetryClient::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

qint64 TelemetryClient::elapsedSinceTelemetryMs() const
{
    return m_lastTelemetry.isValid() ? m_lastTelemetry.elapsed() : -1;
}

QString TelemetryClient::sendControl(const QString &action, const QJsonObject &params)
{
    if (!isConnected()) {
        emit errorOccurred(QStringLiteral("未连接，控制参数未发送"));
        return {};
    }
    if (m_pending.size() >= 64) {
        emit errorOccurred(QStringLiteral("待确认命令过多，请等待设备响应"));
        return {};
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject message{{"type", "control"}, {"id", id},
                              {"action", action}, {"params", params}};
    const QByteArray bytes = RescueProtocol::encode(message);
    if (bytes.isEmpty() || m_socket.write(bytes) != bytes.size()) {
        emit errorOccurred(QStringLiteral("发送控制消息失败"));
        return {};
    }
    m_pending.insert(id, m_sessionClock.elapsed());
    return id;
}

void TelemetryClient::readIncoming()
{
    const auto result = m_decoder.append(m_socket.readAll());
    for (const QString &reason : result.rejectedFrames)
        emit frameRejected(reason);
    if (!result.fatalError.isEmpty()) {
        emit frameRejected(result.fatalError);
        emit errorOccurred(QStringLiteral("协议长度错误，已关闭连接"));
        m_socket.abort();
        return;
    }
    for (const QJsonObject &message : result.messages) {
        const QString type = message.value("type").toString();
        if (type == "telemetry") {
            QString reason;
            if (!RescueProtocol::validateTelemetry(message, &reason)) {
                emit frameRejected(reason);
                continue;
            }
            m_lastTelemetry.start();
            m_staleReported = false;
            if (!m_fresh) {
                m_fresh = true;
                emit dataFreshnessChanged(true);
                emit statusChanged(QStringLiteral("TCP 已连接，遥测正常"));
            }
            emit telemetryReceived(message);
        } else if (type == "ack") {
            const QString id = message.value("id").toString();
            if (!message.value("ok").isBool() || !m_pending.contains(id)) {
                emit frameRejected(QStringLiteral("忽略无效或未知命令的 ACK"));
                continue;
            }
            m_pending.remove(id);
            emit commandAcknowledged(message);
        } else {
            emit frameRejected(QStringLiteral("不支持的消息类型：%1").arg(type));
        }
    }
}

void TelemetryClient::updateTimeouts()
{
    if (!isConnected() || !m_sessionClock.isValid())
        return;
    const qint64 age = m_lastTelemetry.isValid() ? m_lastTelemetry.elapsed()
                                                 : m_sessionClock.elapsed();
    if (age > StaleTimeoutMs && !m_staleReported) {
        m_staleReported = true;
        m_fresh = false;
        emit dataFreshnessChanged(false);
        emit statusChanged(QStringLiteral("遥测超时：超过 3 秒无有效数据"));
    }
    const qint64 now = m_sessionClock.elapsed();
    const auto ids = m_pending.keys();
    for (const QString &id : ids) {
        if (now - m_pending.value(id) > StaleTimeoutMs) {
            m_pending.remove(id);
            emit commandTimedOut(id);
        }
    }
}
