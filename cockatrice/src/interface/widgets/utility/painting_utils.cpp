#include "painting_utils.h"

#include <QPainter>
#include <QTextOption>

void drawOutlinedText(QPainter &painter,
                      const QRect &textRect,
                      const QString &text,
                      Qt::Alignment alignment,
                      const QColor &outlineColor,
                      const QColor &textColor)
{
    // Draw text border by offsetting
    painter.setPen(outlineColor);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx != 0 || dy != 0) {
                painter.drawText(textRect.translated(dx, dy), alignment, text);
            }
        }
    }

    // Draw the main text
    painter.setPen(textColor);
    painter.drawText(textRect, alignment, text);
}

void drawOutlinedText(QPainter &painter,
                      const QRect &textRect,
                      const QString &text,
                      const QTextOption &textOption,
                      const QColor &outlineColor,
                      const QColor &textColor)
{
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
}

