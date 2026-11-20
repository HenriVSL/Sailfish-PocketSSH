#include "terminal/boxdrawing.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

namespace {

// Line weights towards up, right, down, left: 0 none, 1 light, 2 heavy, 3 double.
#define B(u, r, d, l) quint8(((u) << 6) | ((r) << 4) | ((d) << 2) | (l))

const quint8 kLines[0x80] = {
    // 2500
    B(0,1,0,1), B(0,2,0,2), B(1,0,1,0), B(2,0,2,0), 0, 0, 0, 0,
    0, 0, 0, 0, B(0,1,1,0), B(0,2,1,0), B(0,1,2,0), B(0,2,2,0),
    // 2510
    B(0,0,1,1), B(0,0,1,2), B(0,0,2,1), B(0,0,2,2), B(1,1,0,0), B(1,2,0,0), B(2,1,0,0), B(2,2,0,0),
    B(1,0,0,1), B(1,0,0,2), B(2,0,0,1), B(2,0,0,2), B(1,1,1,0), B(1,2,1,0), B(2,1,1,0), B(1,1,2,0),
    // 2520
    B(2,1,2,0), B(2,2,1,0), B(1,2,2,0), B(2,2,2,0), B(1,0,1,1), B(1,0,1,2), B(2,0,1,1), B(1,0,2,1),
    B(2,0,2,1), B(2,0,1,2), B(1,0,2,2), B(2,0,2,2), B(0,1,1,1), B(0,1,1,2), B(0,2,1,1), B(0,2,1,2),
    // 2530
    B(0,1,2,1), B(0,1,2,2), B(0,2,2,1), B(0,2,2,2), B(1,1,0,1), B(1,1,0,2), B(1,2,0,1), B(1,2,0,2),
    B(2,1,0,1), B(2,1,0,2), B(2,2,0,1), B(2,2,0,2), B(1,1,1,1), B(1,1,1,2), B(1,2,1,1), B(1,2,1,2),
    // 2540
    B(2,1,1,1), B(1,1,2,1), B(2,1,2,1), B(2,1,1,2), B(2,2,1,1), B(1,1,2,2), B(1,2,2,1), B(2,2,1,2),
    B(1,2,2,2), B(2,1,2,2), B(2,2,2,1), B(2,2,2,2), 0, 0, 0, 0,
    // 2550
    B(0,3,0,3), B(3,0,3,0), B(0,3,1,0), B(0,1,3,0), B(0,3,3,0), B(0,0,1,3), B(0,0,3,1), B(0,0,3,3),
    B(1,3,0,0), B(3,1,0,0), B(3,3,0,0), B(1,0,0,3), B(3,0,0,1), B(3,0,0,3), B(1,3,1,0), B(3,1,3,0),
    // 2560
    B(3,3,3,0), B(1,0,1,3), B(3,0,3,1), B(3,0,3,3), B(0,3,1,3), B(0,1,3,1), B(0,3,3,3), B(1,3,0,3),
    B(3,1,0,1), B(3,3,0,3), B(1,3,1,3), B(3,1,3,1), B(3,3,3,3), 0, 0, 0,
    // 2570
    0, 0, 0, 0, B(0,0,0,1), B(1,0,0,0), B(0,1,0,0), B(0,0,1,0),
    B(0,0,0,2), B(2,0,0,0), B(0,2,0,0), B(0,0,2,0), B(0,2,0,1), B(1,0,2,0), B(0,1,0,2), B(2,0,1,0),
};
#undef B

enum Dir { Up = 0, Right = 1, Down = 2, Left = 3 };

struct Geometry
{
    QPainter *p;
    QColor color;
    int cx, cy;           // center pixel
    int x0, y0, x1, y1;   // cell edges, x1/y1 exclusive
    int lw;               // light line thickness
    int hw;               // heavy line thickness

    int thick(int w) const { return w == 2 ? hw : lw; }

    // Fill a rectangle given in coordinates relative to the center along direction d:
    // `along` grows towards the cell edge in direction d, `across` is perpendicular
    // (negative = up for horizontal directions, left for vertical ones).
    void fill(Dir d, int a0, int a1, int c0, int c1) const
    {
        if (a1 <= a0 || c1 <= c0)
            return;
        QRect r;
        switch (d) {
        case Right: r = QRect(cx + a0, cy + c0, a1 - a0, c1 - c0); break;
        case Left:  r = QRect(cx - a1 + 1, cy + c0, a1 - a0, c1 - c0); break;
        case Down:  r = QRect(cx + c0, cy + a0, c1 - c0, a1 - a0); break;
        case Up:    r = QRect(cx + c0, cy - a1 + 1, c1 - c0, a1 - a0); break;
        }
        p->fillRect(r, color);
    }

    int edge(Dir d) const
    {
        switch (d) {
        case Right: return x1 - cx;
        case Left:  return cx - x0 + 1;
        case Down:  return y1 - cy;
        case Up:    return cy - y0 + 1;
        }
        return 0;
    }
};

void drawLines(const Geometry &g, quint8 spec)
{
    int w[4];
    w[Up] = (spec >> 6) & 3;
    w[Right] = (spec >> 4) & 3;
    w[Down] = (spec >> 2) & 3;
    w[Left] = spec & 3;
    const int lw = g.lw;
    for (int i = 0; i < 4; ++i) {
        const Dir d = Dir(i);
        if (!w[d])
            continue;
        // Perpendicular neighbours on the negative / positive side, and the opposite.
        const int wn = (d == Right || d == Left) ? w[Up] : w[Left];
        const int wp = (d == Right || d == Left) ? w[Down] : w[Right];
        const int wo = w[(d + 2) % 4];
        const int end = g.edge(d);
        if (w[d] != 3) {
            const int t = g.thick(w[d]);
            int a0;
            if (wn == 3 || wp == 3)
                a0 = -lw - lw / 2; // reach the far line of a double crossing
            else if (wn || wp)
                a0 = -qMax(wn ? g.thick(wn) : 0, wp ? g.thick(wp) : 0) / 2;
            else if (wo)
                a0 = -t / 2;
            else
                a0 = 0;
            g.fill(d, a0, end, -t / 2, -t / 2 + t);
        } else {
            // Two light lines at across offsets -lw and +lw.
            for (int side = -1; side <= 1; side += 2) {
                const int near = side < 0 ? wn : wp; // perpendicular on this line's side
                const int far = side < 0 ? wp : wn;
                int a0;
                if (near == 3)
                    a0 = lw - lw / 2; // stop at the inner line of the double perpendicular
                else if (near)
                    a0 = -g.thick(near) / 2;
                else if (far == 3)
                    a0 = -lw - lw / 2; // corner: reach the outer line
                else if (far)
                    a0 = -g.thick(far) / 2;
                else if (wo)
                    a0 = -lw;
                else
                    a0 = -lw - lw / 2;
                const int c = side * lw;
                g.fill(d, a0, end, c - lw / 2, c - lw / 2 + lw);
            }
        }
    }
}

void drawDashes(const Geometry &g, bool horizontal, int weight, int count)
{
    const int t = g.thick(weight);
    if (horizontal) {
        const qreal seg = qreal(g.x1 - g.x0) / count;
        for (int i = 0; i < count; ++i) {
            const int a = g.x0 + qRound(i * seg);
            const int b = g.x0 + qRound(i * seg + seg * 0.6);
            g.p->fillRect(QRect(a, g.cy - t / 2, qMax(1, b - a), t), g.color);
        }
    } else {
        const qreal seg = qreal(g.y1 - g.y0) / count;
        for (int i = 0; i < count; ++i) {
            const int a = g.y0 + qRound(i * seg);
            const int b = g.y0 + qRound(i * seg + seg * 0.6);
            g.p->fillRect(QRect(g.cx - t / 2, a, t, qMax(1, b - a)), g.color);
        }
    }
}

void drawArc(const Geometry &g, uint ch)
{
    // Center of the light line (matching drawLines' pixel placement).
    const qreal off = -(g.lw / 2) + g.lw / 2.0;
    const qreal cx = g.cx + off;
    const qreal cy = g.cy + off;
    const qreal r = qMin(g.x1 - g.x0, g.y1 - g.y0) / 2.0;
    QPainterPath path;
    switch (ch) {
    case 0x256D: // down and right
        path.moveTo(cx, g.y1);
        path.lineTo(cx, cy + r);
        path.quadTo(cx, cy, cx + r, cy);
        path.lineTo(g.x1, cy);
        break;
    case 0x256E: // down and left
        path.moveTo(cx, g.y1);
        path.lineTo(cx, cy + r);
        path.quadTo(cx, cy, cx - r, cy);
        path.lineTo(g.x0, cy);
        break;
    case 0x256F: // up and left
        path.moveTo(cx, g.y0);
        path.lineTo(cx, cy - r);
        path.quadTo(cx, cy, cx - r, cy);
        path.lineTo(g.x0, cy);
        break;
    default: // 0x2570 up and right
        path.moveTo(cx, g.y0);
        path.lineTo(cx, cy - r);
        path.quadTo(cx, cy, cx + r, cy);
        path.lineTo(g.x1, cy);
        break;
    }
    g.p->save();
    g.p->setRenderHint(QPainter::Antialiasing, true);
    g.p->setBrush(Qt::NoBrush);
    g.p->setPen(QPen(g.color, g.lw, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    g.p->drawPath(path);
    g.p->restore();
}

void drawDiagonal(const Geometry &g, uint ch)
{
    g.p->save();
    g.p->setRenderHint(QPainter::Antialiasing, true);
    g.p->setPen(QPen(g.color, g.lw, Qt::SolidLine, Qt::FlatCap));
    if (ch == 0x2571 || ch == 0x2573)
        g.p->drawLine(QPointF(g.x1, g.y0), QPointF(g.x0, g.y1));
    if (ch == 0x2572 || ch == 0x2573)
        g.p->drawLine(QPointF(g.x0, g.y0), QPointF(g.x1, g.y1));
    g.p->restore();
}

void drawBlock(const Geometry &g, uint ch)
{
    const int w = g.x1 - g.x0;
    const int h = g.y1 - g.y0;
    QPainter *p = g.p;
    auto rect = [&](qreal fx0, qreal fy0, qreal fx1, qreal fy1, const QColor &c) {
        const int a = g.x0 + qRound(fx0 * w), b = g.x0 + qRound(fx1 * w);
        const int t = g.y0 + qRound(fy0 * h), u = g.y0 + qRound(fy1 * h);
        if (b > a && u > t)
            p->fillRect(QRect(a, t, b - a, u - t), c);
    };
    const QColor c = g.color;
    if (ch == 0x2580) {
        rect(0, 0, 1, 0.5, c);
    } else if (ch >= 0x2581 && ch <= 0x2588) { // lower n/8
        rect(0, 1 - (ch - 0x2580) / 8.0, 1, 1, c);
    } else if (ch >= 0x2589 && ch <= 0x258F) { // left n/8, 7/8 down to 1/8
        rect(0, 0, (0x2590 - ch) / 8.0, 1, c);
    } else if (ch == 0x2590) {
        rect(0.5, 0, 1, 1, c);
    } else if (ch >= 0x2591 && ch <= 0x2593) {
        QColor s = c;
        s.setAlphaF(0.25 * (ch - 0x2590));
        rect(0, 0, 1, 1, s);
    } else if (ch == 0x2594) {
        rect(0, 0, 1, 1 / 8.0, c);
    } else if (ch == 0x2595) {
        rect(7 / 8.0, 0, 1, 1, c);
    } else {
        // Quadrants 2596..259F: bits UL, UR, LL, LR
        static const quint8 quads[10] = {
            0x4,       // 2596 lower left
            0x8,       // 2597 lower right
            0x1,       // 2598 upper left
            0x1 | 0x4 | 0x8, // 2599
            0x1 | 0x8, // 259A
            0x1 | 0x2 | 0x4, // 259B
            0x1 | 0x2 | 0x8, // 259C
            0x2,       // 259D upper right
            0x2 | 0x4, // 259E
            0x2 | 0x4 | 0x8, // 259F
        };
        const quint8 q = quads[ch - 0x2596];
        if (q & 0x1) rect(0, 0, 0.5, 0.5, c);
        if (q & 0x2) rect(0.5, 0, 1, 0.5, c);
        if (q & 0x4) rect(0, 0.5, 0.5, 1, c);
        if (q & 0x8) rect(0.5, 0.5, 1, 1, c);
    }
}

void drawPowerline(const Geometry &g, uint ch)
{
    const qreal x0 = g.x0, x1 = g.x1, y0 = g.y0, y1 = g.y1;
    const qreal ym = (y0 + y1) / 2.0;
    g.p->save();
    g.p->setRenderHint(QPainter::Antialiasing, true);
    if (ch == 0xE0B0 || ch == 0xE0B2) {
        QPolygonF poly;
        if (ch == 0xE0B0)
            poly << QPointF(x0, y0) << QPointF(x1, ym) << QPointF(x0, y1);
        else
            poly << QPointF(x1, y0) << QPointF(x0, ym) << QPointF(x1, y1);
        g.p->setPen(Qt::NoPen);
        g.p->setBrush(g.color);
        g.p->drawPolygon(poly);
    } else {
        g.p->setPen(QPen(g.color, g.lw));
        g.p->setBrush(Qt::NoBrush);
        if (ch == 0xE0B1) {
            g.p->drawLine(QPointF(x0, y0), QPointF(x1, ym));
            g.p->drawLine(QPointF(x1, ym), QPointF(x0, y1));
        } else {
            g.p->drawLine(QPointF(x1, y0), QPointF(x0, ym));
            g.p->drawLine(QPointF(x0, ym), QPointF(x1, y1));
        }
    }
    g.p->restore();
}

} // namespace

bool BoxDrawing::draw(QPainter *painter, uint ch, const QRect &cell, const QColor &color, int lightWidth)
{
    if (!handles(ch) || cell.isEmpty())
        return false;
    Geometry g;
    g.p = painter;
    g.color = color;
    g.x0 = cell.left();
    g.y0 = cell.top();
    g.x1 = cell.left() + cell.width();
    g.y1 = cell.top() + cell.height();
    g.cx = g.x0 + cell.width() / 2;
    g.cy = g.y0 + cell.height() / 2;
    g.lw = qMax(1, lightWidth);
    g.hw = qMax(g.lw + 1, g.lw * 2);

    if (ch >= 0xE0B0) {
        drawPowerline(g, ch);
        return true;
    }
    if (ch >= 0x2580) {
        drawBlock(g, ch);
        return true;
    }
    switch (ch) {
    case 0x2504: drawDashes(g, true, 1, 3); return true;
    case 0x2505: drawDashes(g, true, 2, 3); return true;
    case 0x2506: drawDashes(g, false, 1, 3); return true;
    case 0x2507: drawDashes(g, false, 2, 3); return true;
    case 0x2508: drawDashes(g, true, 1, 4); return true;
    case 0x2509: drawDashes(g, true, 2, 4); return true;
    case 0x250A: drawDashes(g, false, 1, 4); return true;
    case 0x250B: drawDashes(g, false, 2, 4); return true;
    case 0x254C: drawDashes(g, true, 1, 2); return true;
    case 0x254D: drawDashes(g, true, 2, 2); return true;
    case 0x254E: drawDashes(g, false, 1, 2); return true;
    case 0x254F: drawDashes(g, false, 2, 2); return true;
    case 0x256D: case 0x256E: case 0x256F: case 0x2570:
        drawArc(g, ch);
        return true;
    case 0x2571: case 0x2572: case 0x2573:
        drawDiagonal(g, ch);
        return true;
    default:
        break;
    }
    const quint8 spec = kLines[ch - 0x2500];
    if (!spec)
        return false;
    drawLines(g, spec);
    return true;
}
