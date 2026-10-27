#include "protocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace RescueProtocol {

QByteArray encode(const QJsonObject &message)
{
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (payload.isEmpty() || payload.size() > MaxPayloadBytes)
        return {};
    QByteArray frame(4, '\0');
    qToBigEndian<quint32>(quint32(payload.size()), frame.data());
    frame.append(payload);
    return frame;
}

void FrameDecoder::clear()
{
    m_buffer.clear();
    m_expected = 0;
}

DecodeResult FrameDecoder::append(const QByteArray &bytes)
{
    DecodeResult result;
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        // Keep the internal buffer bounded to one frame, even when many
        // concatenated frames arrive in the same readyRead() notification.
        if (m_expected == 0) {
            const qsizetype count = std::min(qsizetype(4) - m_buffer.size(),
                                              bytes.size() - offset);
            m_buffer.append(bytes.constData() + offset, count);
            offset += count;
            if (m_buffer.size() < 4)
                break;
            m_expected = qFromBigEndian<quint32>(m_buffer.constData());
            m_buffer.clear();
            if (m_expected == 0 || m_expected > MaxPayloadBytes) {
                result.fatalError = QStringLiteral("非法消息长度：%1（允许 1~%2 字节）")
                                        .arg(m_expected).arg(MaxPayloadBytes);
                clear();
                break; // Lost framing: the caller must close this connection.
            }
        }

        const qsizetype count = std::min(qsizetype(m_expected) - m_buffer.size(),
                                          bytes.size() - offset);
        m_buffer.append(bytes.constData() + offset, count);
        offset += count;
        if (m_buffer.size() != qsizetype(m_expected))
            break;

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(m_buffer, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            result.rejectedFrames.append(QStringLiteral("拒绝非 JSON 对象消息：%1")
                                             .arg(error.errorString()));
        } else {
            result.messages.append(document.object());
        }
        m_buffer.clear();
        m_expected = 0;
    }
    return result;
}

namespace {
bool finiteNumber(const QJsonValue &value)
{
    return value.isDouble() && std::isfinite(value.toDouble());
}

bool validId(const QJsonValue &value)
{
    return (value.isString() && !value.toString().isEmpty()) || finiteNumber(value);
}
} // namespace

bool validateTelemetry(const QJsonObject &message, QString *reason)
{
    const auto reject = [reason](const QString &text) {
        if (reason)
            *reason = text;
        return false;
    };
    if (message.value("type") != QJsonValue("telemetry") || !validId(message.value("id")))
        return reject(QStringLiteral("缺少 telemetry 类型或消息 ID"));
    if (!message.value("simulated").isBool() || !message.value("devices").isObject()
        || !message.value("metrics").isObject() || !message.value("targets").isArray())
        return reject(QStringLiteral("遥测消息字段类型错误"));

    const QJsonObject devices = message.value("devices").toObject();
    for (const char *name : {"camera", "lidar", "jetson"}) {
        if (!devices.value(name).isString())
            return reject(QStringLiteral("设备状态必须包含 camera/lidar/jetson 字符串"));
    }
    const QJsonObject metrics = message.value("metrics").toObject();
    for (const char *name : {"fps", "latency_ms", "cpu_pct", "temp_c", "threshold_m"}) {
        if (!finiteNumber(metrics.value(name)))
            return reject(QStringLiteral("遥测指标缺失或不是有限数值：%1").arg(name));
    }
    if (metrics.value("fps").toDouble() < 0 || metrics.value("latency_ms").toDouble() < 0
        || metrics.value("cpu_pct").toDouble() < 0 || metrics.value("cpu_pct").toDouble() > 100
        || metrics.value("threshold_m").toDouble() <= 0 || !metrics.value("armed").isBool())
        return reject(QStringLiteral("遥测指标范围或 armed 类型错误"));

    const QJsonArray targets = message.value("targets").toArray();
    if (targets.size() > 500)
        return reject(QStringLiteral("单帧目标数超过 500"));
    for (const QJsonValue &value : targets) {
        if (!value.isObject())
            return reject(QStringLiteral("目标必须为 JSON 对象"));
        const QJsonObject target = value.toObject();
        if (!validId(target.value("id")) || !target.value("label").isString()
            || target.value("label").toString().isEmpty())
            return reject(QStringLiteral("目标缺少有效 ID 或类别"));
        for (const char *key : {"confidence", "x", "y", "w", "h"}) {
            const QJsonValue number = target.value(key);
            if (!finiteNumber(number) || number.toDouble() < 0 || number.toDouble() > 1)
                return reject(QStringLiteral("目标 %1 必须在 0~1 之间").arg(key));
        }
        if (target.value("w").toDouble() <= 0 || target.value("h").toDouble() <= 0
            || target.value("x").toDouble() + target.value("w").toDouble() > 1.00001
            || target.value("y").toDouble() + target.value("h").toDouble() > 1.00001)
            return reject(QStringLiteral("目标框超出图像范围"));
        const QJsonValue distance = target.value("distance_m");
        if (!distance.isNull() && (!finiteNumber(distance) || distance.toDouble() < 0))
            return reject(QStringLiteral("距离必须为非负米数或 null"));
    }
    if (reason)
        reason->clear();
    return true;
}

} // namespace RescueProtocol
