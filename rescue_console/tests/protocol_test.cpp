#include "protocol.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QtEndian>

#include <cstdlib>

static void require(bool value, const char *message)
{
    if (!value) {
        QTextStream(stderr) << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static QJsonObject sampleTelemetry()
{
    return {{"type", "telemetry"}, {"id", "test-1"}, {"simulated", true},
            {"devices", QJsonObject{{"camera", "online"}, {"lidar", "online"}, {"jetson", "online"}}},
            {"metrics", QJsonObject{{"fps", 25.0}, {"latency_ms", 42.0}, {"cpu_pct", 20.0},
                                    {"temp_c", 58.0}, {"armed", false}, {"threshold_m", 3.0}}},
            {"targets", QJsonArray{QJsonObject{{"id", "P1"}, {"label", "person"},
                {"confidence", 0.9}, {"x", 0.1}, {"y", 0.2}, {"w", 0.2}, {"h", 0.5},
                {"distance_m", QJsonValue(QJsonValue::Null)}}}}};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QJsonObject message{{"type", "test"}, {"text", QStringLiteral("中文 UTF-8")}};
    const QByteArray frame = RescueProtocol::encode(message);
    RescueProtocol::FrameDecoder decoder;

    for (qsizetype i = 0; i < frame.size(); ++i) {
        const auto result = decoder.append(frame.mid(i, 1));
        require(result.fatalError.isEmpty() && result.rejectedFrames.isEmpty(), "fragment decoding failed");
        require(result.messages.size() == (i == frame.size() - 1 ? 1 : 0), "premature/incomplete fragmented frame");
        if (!result.messages.isEmpty())
            require(result.messages.first() == message, "UTF-8 message changed");
    }
    require(decoder.bufferedBytes() == 0, "buffer not empty after frame");

    const auto multiple = decoder.append(frame + frame + frame);
    require(multiple.messages.size() == 3, "concatenated frames lost");

    QByteArray tooLarge(4, '\0');
    qToBigEndian<quint32>(RescueProtocol::MaxPayloadBytes + 1, tooLarge.data());
    const auto invalidLength = decoder.append(tooLarge + QByteArray(100, 'x'));
    require(!invalidLength.fatalError.isEmpty(), "oversized frame accepted");
    require(decoder.bufferedBytes() == 0, "oversized frame left buffered data");
    require(!decoder.append(QByteArray(4, '\0')).fatalError.isEmpty(), "zero length accepted");
    require(decoder.append(frame).messages.size() == 1, "decoder cannot reset after invalid length");

    QByteArray badJson(4, '\0');
    qToBigEndian<quint32>(4, badJson.data());
    badJson.append("oops");
    const auto malformed = decoder.append(badJson + frame);
    require(malformed.rejectedFrames.size() == 1 && malformed.messages.size() == 1,
            "malformed JSON did not preserve next frame boundary");

    QByteArray arrayJson(4, '\0');
    qToBigEndian<quint32>(2, arrayJson.data());
    arrayJson.append("[]");
    require(decoder.append(arrayJson).rejectedFrames.size() == 1, "non-object JSON accepted");

    auto telemetry = sampleTelemetry();
    QString reason;
    require(RescueProtocol::validateTelemetry(telemetry, &reason), "valid telemetry or null distance rejected");
    telemetry.remove("simulated");
    require(!RescueProtocol::validateTelemetry(telemetry, &reason), "missing simulation flag accepted");
    telemetry = sampleTelemetry();
    auto target = telemetry.value("targets").toArray().first().toObject();
    target.insert("w", 1.0);
    telemetry.insert("targets", QJsonArray{target});
    require(!RescueProtocol::validateTelemetry(telemetry, &reason), "out-of-image target accepted");
    target.insert("w", 0.2);
    target.insert("distance_m", -1.0);
    telemetry.insert("targets", QJsonArray{target});
    require(!RescueProtocol::validateTelemetry(telemetry, &reason), "negative distance accepted");

    QTextStream(stdout) << "PASS: fragmented UTF-8, concatenated frames, invalid lengths, "
                           "malformed JSON recovery, telemetry validation\n";
    return 0;
}
