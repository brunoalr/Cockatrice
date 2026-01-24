/**
 * @file painting_utils.h
 * @ingroup Widgets
 * @brief Utility functions for painting operations.
 */

#ifndef PAINTING_UTILS_H
#define PAINTING_UTILS_H

#include <QColor>
#include <QRect>
#include <QString>
#include <Qt>

class QPainter;
class QTextOption;

/**
 * @brief Draws text with an outline for visibility using QPainterPath.
 * @param painter The painter to draw the text.
 * @param textRect The rectangle area to draw the text in.
 * @param text The text to display.
 * @param textOption The text layout options, such as alignment.
 * @param outlineColor The color of the outline (default: black).
 * @param textColor The color of the main text (default: white).
 * @param outlineWidth The width of the outline stroke (default: 2.0).
 */
void drawOutlinedText(QPainter &painter,
                      const QRect &textRect,
                      const QString &text,
                      const QTextOption &textOption,
                      const QColor &outlineColor = Qt::black,
                      const QColor &textColor = Qt::white,
                      qreal outlineWidth = 2.0);

/**
 * @brief Draws text with an outline for visibility.
 * Convenience overload that converts Qt::Alignment to QTextOption.
 */
void drawOutlinedText(QPainter &painter,
                      const QRect &textRect,
                      const QString &text,
                      Qt::Alignment alignment,
                      const QColor &outlineColor = Qt::black,
                      const QColor &textColor = Qt::white,
                      qreal outlineWidth = 2.0);

#endif // PAINTING_UTILS_H
