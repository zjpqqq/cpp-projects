#include "videowidget.h"

#include <QFontMetrics>
#include <QJsonObject>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <utility>

VideoWidget::VideoWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void VideoWidget::setFrame(QImage frame)
{
    m_frame = std::move(frame);
    update();
}

void VideoWidget::clearFrame()
{
    m_frame = QImage();
    update();
}

void VideoWidget::setTargets(QJsonArray targets)
{
    m_targets = std::move(targets);
    update();
}

void VideoWidget::setSimulated(bool simulated)
{
    m_simulated = simulated;
    update();
}

void VideoWidget::setSourceLabel(QString label)
{
    m_sourceLabel = std::move(label);
    update();
}

void VideoWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#070f1b"));

    if (m_frame.isNull()) {
        painter.setPen(QColor("#6c829d"));
        QFont font = painter.font();
        font.setPointSize(12);
        painter.setFont(font);
        painter.drawText(rect().adjusted(20, 20, -20, -20),
            Qt::AlignCenter | Qt::TextWordWrap,
            QStringLiteral("等待视频画面\n打开本地视频、网络视频流或摄像头"));
        return;
    }

    const QSize fitted = m_frame.size().scaled(size(), Qt::KeepAspectRatio);
    const QRectF area((width() - fitted.width()) / 2.0,
        (height() - fitted.height()) / 2.0, fitted.width(), fitted.height());
    painter.drawImage(area, m_frame);
    painter.save();
    painter.setClipRect(area);

    QFont overlayFont = painter.font();
    overlayFont.setPixelSize(std::clamp(width() / 80, 11, 16));
    overlayFont.setBold(true);
    painter.setFont(overlayFont);
    const QFontMetrics metrics(overlayFont);
    for (const QJsonValue& value : m_targets) {
        if (!value.isObject())
            continue;
        const QJsonObject target = value.toObject();
        const double x = target.value(QStringLiteral("x")).toDouble(-1);
        const double y = target.value(QStringLiteral("y")).toDouble(-1);
        const double w = target.value(QStringLiteral("w")).toDouble(-1);
        const double h = target.value(QStringLiteral("h")).toDouble(-1);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w)
            || !std::isfinite(h) || x < 0 || y < 0 || w <= 0 || h <= 0
            || x > 1 || y > 1)
            continue;
        const QRectF normalized = QRectF(x, y, w, h).intersected(QRectF(0, 0, 1, 1));
        if (normalized.isEmpty())
            continue;
        const QRectF box(area.left() + normalized.left() * area.width(),
            area.top() + normalized.top() * area.height(),
            normalized.width() * area.width(), normalized.height() * area.height());
        const QColor accent = m_simulated ? QColor("#f7b84c") : QColor("#48dbc7");
        painter.setPen(QPen(accent, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(box, 3, 3);

        QString caption = target.value(QStringLiteral("label")).toString(QStringLiteral("目标")).left(40);
        const double confidence = target.value(QStringLiteral("confidence")).toDouble(-1);
        if (std::isfinite(confidence) && confidence >= 0 && confidence <= 1)
            caption += QStringLiteral("  %1%").arg(confidence * 100, 0, 'f', 0);
        const QJsonValue distanceValue = target.value(QStringLiteral("distance_m"));
        const double distance = distanceValue.toDouble(-1);
        if (distanceValue.isDouble() && std::isfinite(distance) && distance >= 0)
            caption += QStringLiteral("  %1 m").arg(distance, 0, 'f', 2);
        else
            caption += QStringLiteral("  距离未知");
        const qreal captionWidth = std::min<qreal>(metrics.horizontalAdvance(caption) + 14, area.width());
        const qreal captionHeight = metrics.height() + 9;
        const qreal captionLeft = std::clamp(box.left(), area.left(), area.right() - captionWidth);
        const qreal captionTop = std::max(area.top(), box.top() - captionHeight);
        const QRectF labelRect(captionLeft, captionTop, captionWidth, captionHeight);
        painter.fillRect(labelRect, accent);
        painter.setPen(QColor("#0b1420"));
        painter.drawText(labelRect.adjusted(7, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
            metrics.elidedText(caption, Qt::ElideRight, static_cast<int>(captionWidth - 12)));
    }

    const QString badgeText = m_simulated ? QStringLiteral("SIMULATION · 模拟目标")
                                         : QStringLiteral("后端目标数据");
    const qreal badgeWidth = std::min<qreal>(metrics.horizontalAdvance(badgeText) + 20, area.width() - 16);
    const QRectF badge(area.left() + 8, area.top() + 8, badgeWidth, metrics.height() + 12);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(5, 14, 26, 220));
    painter.drawRoundedRect(badge, 5, 5);
    painter.setPen(m_simulated ? QColor("#f7b84c") : QColor("#48dbc7"));
    painter.drawText(badge, Qt::AlignCenter, badgeText);

    const QRectF footer(area.left(), area.bottom() - 30, area.width(), 30);
    painter.fillRect(footer, QColor(5, 14, 26, 180));
    painter.setPen(QColor("#b5c5d7"));
    painter.drawText(footer.adjusted(12, 0, -12, 0), Qt::AlignVCenter | Qt::AlignLeft,
        metrics.elidedText(m_sourceLabel, Qt::ElideMiddle, static_cast<int>(area.width() - 24)));
    painter.restore();
}

QImage VideoWidget::makeDemoFrame(double seconds)
{
    QImage frame(1280, 720, QImage::Format_RGB32);
    QPainter painter(&frame);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor cyan("#4a9da8");

    QLinearGradient sky(0, 0, 0, 460);
    sky.setColorAt(0, QColor("#213c50"));
    sky.setColorAt(1, QColor("#8c9b9c"));
    painter.fillRect(frame.rect(), sky);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#314958"));
    painter.drawPolygon(QPolygonF{QPointF(0, 70), QPointF(490, 230), QPointF(490, 510), QPointF(0, 720)});
    painter.setBrush(QColor("#4c6268"));
    painter.drawPolygon(QPolygonF{QPointF(890, 190), QPointF(1280, 90), QPointF(1280, 720), QPointF(890, 485)});
    painter.setBrush(QColor("#263f4c"));
    painter.drawRect(515, 220, 345, 250);
    painter.setPen(QPen(QColor("#57717a"), 3));
    for (int i = 0; i < 9; ++i)
        painter.drawLine(530, 245 + i * 23, 845, 245 + i * 23);

    QLinearGradient ground(0, 400, 0, 720);
    ground.setColorAt(0, QColor("#6b7776"));
    ground.setColorAt(1, QColor("#263e4a"));
    painter.setPen(Qt::NoPen);
    painter.setBrush(ground);
    painter.drawPolygon(QPolygonF{QPointF(0, 720), QPointF(470, 390), QPointF(890, 390), QPointF(1280, 720)});
    painter.setPen(QPen(QColor(165, 182, 178, 65), 1));
    for (int i = 0; i < 7; ++i) {
        const double d = static_cast<double>(i) / 6.0;
        const double yy = 405 + d * d * 315;
        const double left = 470 * (1 - d * d);
        const double right = 890 + 390 * d * d;
        painter.drawLine(QPointF(left, yy), QPointF(right, yy));
    }
    for (int i = 0; i <= 6; ++i)
        painter.drawLine(QPointF(530 + i * 48, 390), QPointF(80 + i * 185, 720));

    // Crates and a lane stripe create a visible, intentionally synthetic scene.
    painter.setPen(QPen(QColor("#73817b"), 2));
    painter.setBrush(QColor("#596858"));
    painter.drawRect(980, 400, 190, 145);
    painter.drawRect(1010, 330, 130, 70);
    painter.drawLine(1075, 400, 1075, 545);
    painter.setPen(QPen(QColor("#bba76b"), 8));
    painter.drawLine(280, 720, 560, 432);
    painter.drawLine(1120, 720, 850, 432);

    const double sway = 18.0 * std::sin(seconds * 0.7);
    const double leg = 8.0 * std::sin(seconds * 3.0);
    painter.save();
    painter.translate(610 + sway, 365);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 55));
    painter.drawEllipse(QRectF(-35, 183, 90, 15));
    painter.setPen(QPen(QColor("#182c38"), 15, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(-9, 108), QPointF(-19 - leg, 185));
    painter.drawLine(QPointF(11, 108), QPointF(28 + leg, 185));
    painter.setPen(QPen(QColor("#233a44"), 13, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(-22, 48), QPointF(-36 + leg * 0.7, 114));
    painter.drawLine(QPointF(22, 48), QPointF(38 - leg * 0.7, 112));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#e39a3e"));
    painter.drawRoundedRect(QRectF(-25, 35, 51, 83), 13, 13);
    painter.setBrush(QColor("#e2d4ad"));
    painter.drawRect(QRectF(-25, 66, 51, 8));
    painter.drawRect(QRectF(-10, 36, 8, 80));
    painter.drawRect(QRectF(10, 36, 8, 80));
    painter.setBrush(QColor("#b0a38b"));
    painter.drawEllipse(QRectF(-17, -3, 34, 41));
    painter.setBrush(QColor("#f3bb43"));
    painter.drawPie(QRectF(-21, -9, 42, 38), 0, 180 * 16);
    painter.drawRoundedRect(QRectF(-24, 7, 48, 7), 3, 3);
    painter.restore();

    painter.setPen(QPen(cyan, 1));
    painter.drawLine(628, 325, 628, 346);
    painter.drawLine(618, 335, 638, 335);
    painter.setPen(QColor(205, 223, 228, 110));
    QFont font = painter.font();
    font.setPixelSize(15);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 2);
    painter.setFont(font);
    painter.drawText(QRect(30, 625, 1200, 45), Qt::AlignRight | Qt::AlignBottom,
        QStringLiteral("SYNTHETIC SCENE  /  NO CAMERA · NO YOLO"));
    return frame;
}
