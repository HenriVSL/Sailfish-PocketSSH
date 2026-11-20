#ifndef BOXDRAWING_H
#define BOXDRAWING_H

#include <QColor>
#include <QRect>

class QPainter;

// Draws box-drawing characters (U+2500..U+257F), block elements (U+2580..U+259F) and
// the Powerline separators (U+E0B0..U+E0B3) as shapes filling the whole cell, so that
// lines join seamlessly between cells whatever the font's line height is.
namespace BoxDrawing {

inline bool handles(uint ch)
{
    return (ch >= 0x2500 && ch <= 0x259F) || (ch >= 0xE0B0 && ch <= 0xE0B3);
}

// cell: the character's cell in device pixels (2 cells wide for wide characters).
// lightWidth: thickness of a light line in pixels. Returns false if `ch` is not one
// it draws (the caller then falls back to the font).
bool draw(QPainter *painter, uint ch, const QRect &cell, const QColor &color, int lightWidth);

} // namespace BoxDrawing

#endif // BOXDRAWING_H
