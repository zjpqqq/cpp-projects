#pragma once

#include <QImage>
#include <QJsonArray>
#include <QWidget>

class VideoWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget* parent = nullptr);
    void setFrame(QImage frame);
    void clearFrame();
    void setTargets(QJsonArray targets);
    void setSimulated(bool simulated);
    void setSourceLabel(QString label);

    // Synthetic illustration only; this function does not perform inference.
    static QImage makeDemoFrame(double seconds);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage m_frame;
    QJsonArray m_targets;
    QString m_sourceLabel = QStringLiteral("等待画面");
    bool m_simulated = false;
};
