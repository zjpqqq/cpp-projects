#include "protocol.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QPointer>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>

#include <cmath>
#include <memory>

// This program generates synthetic telemetry only. No sensor or inference
// library is used, and every generated telemetry frame is marked simulated.
class MockJetson : public QObject {
public:
    explicit MockJetson(QObject *parent = nullptr) : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *socket = m_server.nextPendingConnection();
                socket->setReadBufferSize(RescueProtocol::MaxPayloadBytes + 4);
                m_clients.insert(socket, RescueProtocol::FrameDecoder{});
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                    m_clients.remove(socket);
                    socket->deleteLater();
                });
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    receive(socket);
                });
                socket->write(RescueProtocol::encode(telemetry()));
            }
        });
        m_timer.setInterval(200);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            const QByteArray frame = RescueProtocol::encode(telemetry());
            const auto clients = m_clients.keys();
            for (QTcpSocket *socket : clients) {
                // Bound queued output for a client that stops reading.
                if (socket->bytesToWrite() > 1024 * 1024) {
                    socket->abort();
                    continue;
                }
                socket->write(frame);
            }
        });
    }

    bool listen(quint16 port)
    {
        if (!m_server.listen(QHostAddress::LocalHost, port))
            return false;
        m_clock.start();
        m_timer.start();
        return true;
    }
    quint16 port() const { return m_server.serverPort(); }
    QString errorString() const { return m_server.errorString(); }

private:
    QJsonObject telemetry()
    {
        const double seconds = m_clock.isValid() ? m_clock.elapsed() / 1000.0 : 0;
        const QJsonArray targets{
            QJsonObject{{"id", "SIM-01"}, {"label", "person"}, {"confidence", 0.94},
                        {"x", 0.23 + 0.035 * std::sin(seconds * 0.4)}, {"y", 0.24},
                        {"w", 0.15}, {"h", 0.53},
                        {"distance_m", 2.7 + 0.8 * std::sin(seconds * 0.25)}},
            QJsonObject{{"id", "SIM-02"}, {"label", "person"}, {"confidence", 0.88},
                        {"x", 0.66 + 0.02 * std::sin(seconds * 0.3)}, {"y", 0.31},
                        {"w", 0.10}, {"h", 0.38}, {"distance_m", 6.4}}};
        const QJsonObject metrics{
            {"fps", 25.0}, {"latency_ms", 42.0 + 4.0 * std::sin(seconds)},
            {"cpu_pct", 31.0 + 3.0 * std::sin(seconds * 0.5)},
            {"temp_c", 58.0 + std::sin(seconds * 0.2)},
            {"armed", m_armed}, {"threshold_m", m_threshold}};
        return {{"type", "telemetry"}, {"id", QString("sim-%1").arg(++m_sequence)},
                {"simulated", true},
                {"devices", QJsonObject{{"camera", "online"}, {"lidar", "online"},
                                         {"jetson", "online"}}},
                {"metrics", metrics}, {"targets", targets}};
    }

    void receive(QTcpSocket *socket)
    {
        if (!m_clients.contains(socket))
            return;
        const auto decoded = m_clients[socket].append(socket->readAll());
        if (!decoded.fatalError.isEmpty()) {
            socket->abort();
            return;
        }
        for (const QJsonObject &message : decoded.messages) {
            const QString id = message.value("id").toString();
            const QString action = message.value("action").toString();
            const QJsonObject params = message.value("params").toObject();
            bool ok = false;
            QString reason = QStringLiteral("无效命令或参数");
            if (message.value("type").toString() == "control" && !id.isEmpty()
                && message.value("params").isObject()) {
                if (action == "arm" && params.value("armed").isBool()) {
                    m_armed = params.value("armed").toBool();
                    ok = true;
                } else if (action == "threshold_m" && params.value("value").isDouble()) {
                    const double value = params.value("value").toDouble();
                    if (std::isfinite(value) && value >= 0.1 && value <= 100.0) {
                        m_threshold = value;
                        ok = true;
                    } else {
                        reason = QStringLiteral("阈值范围为 0.1~100 米");
                    }
                }
            }
            const QJsonObject ack{{"type", "ack"}, {"id", id}, {"action", action},
                                  {"ok", ok}, {"simulated", true},
                                  {"message", ok ? QStringLiteral("模拟设备已应用参数") : reason}};
            socket->write(RescueProtocol::encode(ack));
            if (ok)
                socket->write(RescueProtocol::encode(telemetry()));
        }
    }

    QTcpServer m_server;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QHash<QTcpSocket *, RescueProtocol::FrameDecoder> m_clients;
    quint64 m_sequence = 0;
    bool m_armed = false;
    double m_threshold = 3.0;
};

static int runSelfTest(QCoreApplication &app)
{
    MockJetson server;
    if (!server.listen(0)) {
        QTextStream(stderr) << "FAIL: cannot listen: " << server.errorString() << '\n';
        return 2;
    }
    QTcpSocket client;
    RescueProtocol::FrameDecoder decoder;
    QSet<QString> ackIds;
    bool telemetryMatches = false;
    auto completed = std::make_shared<bool>(false);
    QObject::connect(&client, &QTcpSocket::connected, &app, [&] {
        const QByteArray one = RescueProtocol::encode({{"type", "control"},
            {"id", "test-threshold"}, {"action", "threshold_m"},
            {"params", QJsonObject{{"value", 2.5}}}});
        const QByteArray two = RescueProtocol::encode({{"type", "control"},
            {"id", "test-arm"}, {"action", "arm"},
            {"params", QJsonObject{{"armed", true}}}});
        // Fragment the first header, then concatenate the rest with frame two.
        client.write(one.left(3));
        QTimer::singleShot(30, &client, [&, one, two] {
            client.write(one.mid(3) + two);
        });
    });
    QObject::connect(&client, &QTcpSocket::readyRead, &app, [&] {
        const auto decoded = decoder.append(client.readAll());
        if (!decoded.fatalError.isEmpty() || !decoded.rejectedFrames.isEmpty()) {
            QTextStream(stderr) << "FAIL: malformed server response\n";
            *completed = true;
            app.exit(3);
            return;
        }
        for (const QJsonObject &message : decoded.messages) {
            if (message.value("type").toString() == "ack") {
                if (!message.value("ok").toBool() || !message.value("simulated").toBool()) {
                    QTextStream(stderr) << "FAIL: control rejected\n";
                    *completed = true;
                    app.exit(4);
                    return;
                }
                ackIds.insert(message.value("id").toString());
            } else if (message.value("type").toString() == "telemetry") {
                QString reason;
                if (!RescueProtocol::validateTelemetry(message, &reason)) {
                    QTextStream(stderr) << "FAIL: telemetry: " << reason << '\n';
                    *completed = true;
                    app.exit(5);
                    return;
                }
                const QJsonObject metrics = message.value("metrics").toObject();
                if (metrics.value("armed").toBool()
                    && std::abs(metrics.value("threshold_m").toDouble() - 2.5) < 1e-9
                    && message.value("simulated").toBool())
                    telemetryMatches = true;
            }
        }
        if (!*completed && ackIds.contains("test-threshold") && ackIds.contains("test-arm")
            && telemetryMatches) {
            *completed = true;
            QTextStream(stdout) << "PASS: real loopback TCP, split header, concatenated commands, "
                                   "two ACKs and applied simulated telemetry\n";
            app.exit(0);
        }
    });
    QTimer::singleShot(5000, &app, [&] {
        if (!*completed) {
            *completed = true;
            QTextStream(stderr) << "FAIL: loopback self-test timed out\n";
            app.exit(6);
        }
    });
    client.connectToHost(QHostAddress::LocalHost, server.port());
    return app.exec();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("mock_jetson");
    QCommandLineParser parser;
    parser.setApplicationDescription("Synthetic Jetson telemetry server; no real sensors or YOLO inference.");
    parser.addHelpOption();
    parser.addOption({{"p", "port"}, "Loopback TCP port; 0 selects a free port (default 9100).", "port", "9100"});
    parser.addOption({"self-test", "Test real loopback TCP framing, ACK and parameter application, then exit."});
    parser.process(app);
    if (parser.isSet("self-test"))
        return runSelfTest(app);
    bool valid = false;
    const uint port = parser.value("port").toUInt(&valid);
    if (!valid || port > 65535) {
        QTextStream(stderr) << "Invalid port\n";
        return 1;
    }
    MockJetson server;
    if (!server.listen(quint16(port))) {
        QTextStream(stderr) << "Listen failed: " << server.errorString() << '\n';
        return 1;
    }
    QTextStream(stdout) << "READY " << server.port() << Qt::endl
                        << "SIMULATION ONLY: listening on 127.0.0.1:" << server.port()
                        << "; all telemetry is synthetic." << Qt::endl;
    return app.exec();
}
