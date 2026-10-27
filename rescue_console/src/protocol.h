#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

namespace RescueProtocol {

constexpr quint32 MaxPayloadBytes = 4u * 1024u * 1024u;

// Each frame is: unsigned 32-bit big-endian byte count + UTF-8 JSON object.
QByteArray encode(const QJsonObject &message);

struct DecodeResult {
    QVector<QJsonObject> messages;
    QStringList rejectedFrames;
    QString fatalError;
};

class FrameDecoder {
public:
    DecodeResult append(const QByteArray &bytes);
    void clear();
    qsizetype bufferedBytes() const { return m_buffer.size(); }

private:
    QByteArray m_buffer;
    quint32 m_expected = 0;
};

// Coordinates in telemetry targets use normalized [0, 1] image coordinates.
bool validateTelemetry(const QJsonObject &message, QString *reason = nullptr);

} // namespace RescueProtocol
