#include "painting_utils.h"

#include <QPainter>
#include <QTextOption>

void drawOutlinedText(QPainter &painter,
                      const QRect &textRect,
                      const QString &text,
                      const QTextOption &textOption,
                      const QColor &outlineColor,
                      const QColor &textColor)
{
    painter.save();

    // Draw text border by offsetting
    painter.setPen(outlineColor);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx != 0 || dy != 0) {
                QRect shiftedTextRect = textRect.translated(dx, dy);
                painter.drawText(shiftedTextRect, text, textOption);
            }
        }
    }

    // Draw the main text
    painter.setPen(textColor);
    painter.drawText(textRect, text, textOption);

    painter.restore();
}

void drawOutlinedText(QPainter &painter,
                      const QRect &textRect,
                      const QString &text,
                      Qt::Alignment alignment,
                      const QColor &outlineColor,
                      const QColor &textColor)
{
    // Convert Qt::Alignment to QTextOption and delegate to the main implementation
    QTextOption textOption;
    textOption.setAlignment(alignment);
    textOption.setWrapMode(QTextOption::NoWrap);
    drawOutlinedText(painter, textRect, text, textOption, outlineColor, textColor);
}
