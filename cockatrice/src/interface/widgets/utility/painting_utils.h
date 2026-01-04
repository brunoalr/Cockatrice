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
 * @brief Draws text with an outline for visibility.
 * @param painter The painter to draw the text.
 * @param textRect The rectangle area to draw the text in.
 * @param text The text to display.
 * @param textOption The text layout options, such as alignment.
 * @param outlineColor The color of the outline (default: black).
 * @param textColor The color of the main text (default: white).
 *
 * Draws an outline around the text by offsetting and drawing the text multiple times,
 * then draws the main text on top. This enhances readability on complex backgrounds.
 * This is the core implementation that handles all text drawing.
 */
void drawOutlinedText(QPainter &painter,
                      const QRect &textRect,
                      const QString &text,
                      const QTextOption &textOption,
                      const QColor &outlineColor = Qt::black,
                      const QColor &textColor = Qt::white);

/**
 * @brief Draws text with an outline for visibility.
 * @param painter The painter to draw the text.
 * @param textRect The rectangle area to draw the text in.
 * @param text The text to display.
 * @param alignment The text alignment flags.
 * @param outlineColor The color of the outline (default: black).
 * @param textColor The color of the main text (default: white).
 *
 * Convenience overload that converts Qt::Alignment to QTextOption and calls
 * the main implementation. Use this when you only need simple alignment.
 */
void drawOutlinedText(QPainter &painter,
                      const QRect &textRect,
                      const QString &text,
                      Qt::Alignment alignment,
                      const QColor &outlineColor = Qt::black,
                      const QColor &textColor = Qt::white);

#endif // PAINTING_UTILS_H
