#include "painting_utils.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QTextOption>

void drawOutlinedText(QPainter &painter,
                      const QRect &textRect,
                      const QString &text,
                      const QTextOption &textOption,
                      const QColor &outlineColor,
                      const QColor &textColor,
                      qreal outlineWidth)
{
    if (text.isEmpty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QFont font = painter.font();
    const QFontMetricsF fm(font);

    // Build text path at origin
    QPainterPath textPath;
    textPath.setFillRule(Qt::WindingFill);
    const QStringList lines = text.split('\n');
    qreal y = fm.ascent();
    for (const QString &line : lines) {
        textPath.addText(0, y, font, line);
        y += fm.height();
    }

    // Calculate alignment offset
    const QRectF pathBounds = textPath.boundingRect();
    const QRectF targetRect(textRect);
    const Qt::Alignment alignment = textOption.alignment();

    qreal tx = 0;
    qreal ty = 0;

    // Horizontal alignment
    if (alignment & Qt::AlignRight) {
        tx = targetRect.right() - pathBounds.right();
    } else if (alignment & Qt::AlignHCenter) {
        tx = targetRect.center().x() - pathBounds.center().x();
    } else { // AlignLeft (default)
        tx = targetRect.left() - pathBounds.left();
    }

    // Vertical alignment
    if (alignment & Qt::AlignBottom) {
        ty = targetRect.bottom() - pathBounds.bottom();
    } else if (alignment & Qt::AlignVCenter) {
        ty = targetRect.center().y() - pathBounds.center().y();
    } else { // AlignTop (default)
        ty = targetRect.top() - pathBounds.top();
    }

    textPath.translate(tx, ty);

    // Draw outline then fill
    painter.strokePath(textPath, QPen(outlineColor, outlineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.fillPath(textPath, textColor);

    painter.restore();
}

void drawOutlinedText(QPainter &painter,
                      const QRect &textRect,
                      const QString &text,
                      Qt::Alignment alignment,
                      const QColor &outlineColor,
                      const QColor &textColor,
                      qreal outlineWidth)
{
    // Convert Qt::Alignment to QTextOption and delegate to the main implementation
    QTextOption textOption;
    textOption.setAlignment(alignment);
    textOption.setWrapMode(QTextOption::NoWrap);
    drawOutlinedText(painter, textRect, text, textOption, outlineColor, textColor, outlineWidth);
}
