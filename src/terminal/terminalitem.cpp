#include "terminal/terminalitem.h"

#include "terminal/boxdrawing.h"
#include "terminal/terminal_p.h"

#include <QClipboard>
#include <QDateTime>
#include <cstdio>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QRawFont>
#include <QGuiApplication>
#include <QHash>
#include <QInputMethod>
#include <QPainter>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QStyleHints>
#include <QThread>
#include <QTimer>
#include <QVarLengthArray>
#include <QtMath>

#include <cmath>

namespace {

// Pocket Shell dark palette (iOS Theme.swift).
const QRgb kBackground = 0xFF0D0F14;
const QRgb kForeground = 0xFFDADFE8;
const QRgb kCursor = 0xFF4AC9A3;
const QRgb kAnsi[16] = {
    0xFF1B1E26, 0xFFF0707A, 0xFF8FD18B, 0xFFE9C46A, 0xFF6FA8F7, 0xFFC792EA, 0xFF56C8D8, 0xFFC8CDD6,
    0xFF4B5263, 0xFFFF8A93, 0xFFA8E6A3, 0xFFFFD98A, 0xFF8DBBFF, 0xFFDDB0FF, 0xFF7FDCEA, 0xFFF2F4F8,
};

const qreal kMinFontPx = 10;
const qreal kMaxFontPx = 64;
const qreal kDefaultFontPx = 22; // ~53 columns on a 720 px wide portrait screen
const qreal kDefaultMargin = 8;
const int kLongPressMs = 600;

const QRgb *palette256()
{
    static QRgb table[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 16; ++i)
            table[i] = kAnsi[i];
        static const int levels[6] = {0, 95, 135, 175, 215, 255};
        for (int i = 0; i < 216; ++i)
            table[16 + i] = qRgb(levels[i / 36], levels[(i / 6) % 6], levels[i % 6]);
        for (int i = 0; i < 24; ++i) {
            const int v = 8 + 10 * i;
            table[232 + i] = qRgb(v, v, v);
        }
        init = true;
    }
    return table;
}

inline QRgb resolve(const VTermColor &c)
{
    if (c.type & VTERM_COLOR_DEFAULT_FG)
        return kForeground;
    if (c.type & VTERM_COLOR_DEFAULT_BG)
        return kBackground;
    if (VTERM_COLOR_IS_INDEXED(&c))
        return palette256()[c.indexed.idx];
    return qRgb(c.rgb.red, c.rgb.green, c.rgb.blue);
}

inline QRgb blend(QRgb a, QRgb b, qreal t) // t = weight of a
{
    return qRgb(qRound(qRed(a) * t + qRed(b) * (1 - t)), qRound(qGreen(a) * t + qGreen(b) * (1 - t)),
                qRound(qBlue(a) * t + qBlue(b) * (1 - t)));
}

const QRgb kSelection = blend(kCursor, kBackground, 0.38);

bool touchDebugEnabled()
{
    static const bool on = qEnvironmentVariableIsSet("POCKETSSH_TOUCH_DEBUG");
    return on;
}

bool isWordChar(uint c)
{
    if (c == 0 || c == ' ' || c == '\t')
        return false;
    if (c >= 0x2500 && c <= 0x259F)
        return false;
    switch (c) {
    case '"': case '\'': case '`': case '(': case ')': case '[': case ']': case '{': case '}':
    case '<': case '>': case '|': case ',': case ';':
        return false;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------

class TerminalItem::Private
{
public:
    explicit Private(TerminalItem *q);

    // Receives the (touch-synthesized) mouse events for the whole item. It is a child so
    // that TerminalItem, as its filtering parent, sees the raw touch events too and can
    // take over two-finger pinches, like PinchArea does with a MouseArea inside.
    class TouchArea : public QQuickItem
    {
    public:
        TouchArea(Private *d, QQuickItem *parent)
            : QQuickItem(parent)
            , d(d)
        {
            setAcceptedMouseButtons(Qt::LeftButton);
        }

    protected:
        void mousePressEvent(QMouseEvent *e) override
        {
            if (touchDebugEnabled())
                fprintf(stderr, "TOUCH area press %.0f,%.0f\n", e->localPos().x(), e->localPos().y());
            d->press(e->localPos());
            e->accept();
        }
        void mouseMoveEvent(QMouseEvent *e) override { d->move(e->localPos()); e->accept(); }
        void mouseReleaseEvent(QMouseEvent *e) override { d->release(e->localPos()); e->accept(); }
        void mouseUngrabEvent() override
        {
            if (touchDebugEnabled())
                fprintf(stderr, "TOUCH area mouseUngrab\n");
            d->cancelGesture();
        }
        void mouseDoubleClickEvent(QMouseEvent *e) override { e->accept(); }

    private:
        Private *d;
    };

    TerminalItem *q;
    QPointer<Terminal> term;
    TouchArea *area = nullptr;

    qreal fontPx = kDefaultFontPx;
    QString family = QStringLiteral("DejaVu Sans Mono");
    bool resizes = true;
    qreal margin = kDefaultMargin;

    // Metrics of the font actually used (in cover mode it is scaled to the width).
    QFont fonts[4]; // regular, bold, italic, bold italic
    QRawFont rawFonts[4];
    QVector<QPointF> glyphPositions;
    qreal cellW = 1;
    int cellH = 1;
    qreal ascent = 0;
    qreal underlinePos = 1;
    qreal strikePos = 0;
    int lineW = 1;
    int usedPx = 0;
    bool metricsValid = false;
    QHash<uint, bool> coverage;  // code point -> in the primary font
    mutable qreal coverRatio = 0.6; // cell width / pixel size of `family`
    mutable QString coverRatioFamily;
    QHash<quint64, qreal> advances; // (style << 32 | ch) -> advance

    // Scrolling, in pixels from the bottom (0 = live screen).
    qreal scrollPx = 0;
    bool wasScrolledBack = false;
    qreal altAccum = 0;
    quint64 seenPushed = 0;
    quint64 seenReset = 0;

    // Selection in history coordinates (see Terminal::Private::line), inclusive.
    bool selActive = false;
    QPoint selStart, selEnd;          // x = column, y = row
    QPoint anchorStart, anchorEnd;    // word under the long press

    // Gestures
    enum Mode { Idle, Pending, Vertical, Horizontal, Selecting };
    Mode mode = Idle;
    QPointF pressPos;
    QPointF lastPos;
    bool pressStoppedFlick = false;
    QTimer longPress;
    struct Sample { qint64 t; qreal y; };
    QVector<Sample> samples;
    QElapsedTimer clock;
    QTimer flickTimer;
    qreal flickVelocity = 0; // px/s, positive = finger moving down (towards history)
    qint64 flickLast = 0;

    bool pinching = false;
    int pinchIds[2] = {-1, -1};
    QPointF pinchPos[2];
    qreal pinchStartDist = 1;
    qreal pinchStartFont = kDefaultFontPx;

    QTimer resizeTimer;
    bool everResized = false;

    QString preedit;

    Terminal::Private *td() const { return term ? term->d : nullptr; }

    void invalidateMetrics();
    void ensureMetrics();
    qreal coverFontPx() const;
    int contentRows() const;
    qreal topRow() const; // first visible history row (fractional)

    void scheduleResize();
    void applyResize();

    void contentChanged();
    void inputSent();
    void setScrollPx(qreal px);
    qreal maxScrollPx() const;
    bool scrollBy(qreal dy);
    void updateScrolledBack();

    QPoint cellAt(const QPointF &pos) const; // history row / column
    QRectF cursorRect() const;

    void press(const QPointF &pos);
    void move(const QPointF &pos);
    void release(const QPointF &pos);
    void cancelGesture();
    void startSelection();
    void extendSelection(const QPoint &cell);
    void tap(const QPointF &pos);
    QString urlAt(const QPoint &cell) const;
    void flickStep();
    void stopFlick();

    bool pinchTouch(QTouchEvent *e);
    void holdMouseGrab();
    void endPinch();

    bool selected(int row, int col) const;
    void drawRow(QPainter *p, const TermLine &line, int row, int y, int cols, int ox);
    void drawCellText(QPainter *p, const TermLine &line, int col, int combOffset, int x, int w, int y, QRgb fg,
                      int style);
    qreal advance(int style, const QString &s, uint key);
    bool inPrimaryFont(uint ch);
    int xAt(int col, int ox) const { return ox + qRound(col * cellW); }
};

TerminalItem::Private::Private(TerminalItem *q)
    : q(q)
{
    clock.start();
    longPress.setSingleShot(true);
    longPress.setInterval(kLongPressMs);
    QObject::connect(&longPress, &QTimer::timeout, q, [this]() { startSelection(); });
    flickTimer.setInterval(16);
    QObject::connect(&flickTimer, &QTimer::timeout, q, [this]() { flickStep(); });
    resizeTimer.setSingleShot(true);
    QObject::connect(&resizeTimer, &QTimer::timeout, q, [this]() { applyResize(); });
}

// --- metrics ---------------------------------------------------------------

void TerminalItem::Private::invalidateMetrics()
{
    metricsValid = false;
}

qreal TerminalItem::Private::coverFontPx() const
{
    // Font size at which the terminal's columns fill the width.
    const Terminal::Private *t = td();
    const int cols = t ? t->cols : 80;
    if (coverRatioFamily != family) {
        QFont f(family);
        f.setPixelSize(100);
        coverRatio = QFontMetricsF(f).width(QLatin1Char('M')) / 100.0;
        coverRatioFamily = family;
    }
    const qreal ratio = coverRatio;
    const qreal avail = q->width() - 2 * margin;
    if (ratio <= 0 || avail <= 0)
        return fontPx;
    return qMax(qreal(2), std::floor(avail / (cols * ratio)));
}

void TerminalItem::Private::ensureMetrics()
{
    const int px = qMax(2, qRound(resizes ? fontPx : coverFontPx()));
    if (metricsValid && px == usedPx)
        return;
    const qreal oldW = cellW;
    const int oldH = cellH;
    for (int i = 0; i < 4; ++i) {
        QFont f(family);
        f.setPixelSize(px);
        f.setStyleHint(QFont::TypeWriter);
        f.setFixedPitch(true);
        f.setKerning(false);
        f.setBold(i & 1);
        f.setItalic(i & 2);
        fonts[i] = f;
        rawFonts[i] = QRawFont::fromFont(f);
    }
    const QFontMetricsF fm(fonts[0]);
    cellW = fm.width(QLatin1Char('M'));
    cellH = qMax(1, qCeil(fm.ascent() + fm.descent()));
    ascent = fm.ascent() + (cellH - fm.ascent() - fm.descent()) / 2;
    underlinePos = qMax(qreal(1), fm.underlinePos());
    strikePos = fm.strikeOutPos();
    lineW = qMax(1, qRound(px / 14.0));
    usedPx = px;
    advances.clear();
    metricsValid = true;
    if (!qFuzzyCompare(oldW, cellW) || oldH != cellH) {
        // paint() runs on the scene graph's render thread (GUI thread blocked); emit
        // from the GUI thread.
        if (QThread::currentThread() == q->thread())
            emit q->cellSizeChanged();
        else
            QMetaObject::invokeMethod(q, "cellSizeChanged", Qt::QueuedConnection);
    }
}

int TerminalItem::Private::contentRows() const
{
    return qMax(1, int((q->height() - 2 * margin) / cellH));
}

qreal TerminalItem::Private::topRow() const
{
    const Terminal::Private *t = td();
    if (!t)
        return 0;
    if (resizes)
        return -scrollPx / cellH;
    // Cover: show the rows around the cursor when they don't all fit.
    const int visible = contentRows();
    if (visible >= t->rows)
        return 0;
    return qBound(0, t->cursor.row + 1 - visible, t->rows - visible);
}

// --- sizing ----------------------------------------------------------------

void TerminalItem::Private::scheduleResize()
{
    if (!resizes || !term || pinching)
        return;
    resizeTimer.start(everResized ? 120 : 0);
}

void TerminalItem::Private::applyResize()
{
    if (!resizes || !term || pinching)
        return;
    ensureMetrics();
    const qreal w = q->width() - 2 * margin;
    const qreal h = q->height() - 2 * margin;
    const int cols = int(w / cellW);
    const int rows = int(h / cellH);
    if (cols < 2 || rows < 1)
        return;
    everResized = true;
    if (cols != term->columns() || rows != term->rows())
        term->resize(cols, rows);
}

// --- terminal updates ------------------------------------------------------

void TerminalItem::Private::contentChanged()
{
    Terminal::Private *t = td();
    if (!t)
        return;
    ensureMetrics();
    const qint64 pushed = qint64(t->sbPushed - seenPushed);
    seenPushed = t->sbPushed;
    if (t->sbReset != seenReset) {
        seenReset = t->sbReset;
        if (selActive && (selStart.y() < 0 || selEnd.y() < 0))
            q->clearSelection();
    }
    if (pushed > 0) {
        // Lines moved up into scrollback: keep a scrolled-back view where it is, and
        // move the selection with its text.
        if (scrollPx > 0)
            scrollPx += pushed * cellH;
        if (selActive) {
            selStart.ry() -= int(pushed);
            selEnd.ry() -= int(pushed);
            anchorStart.ry() -= int(pushed);
            anchorEnd.ry() -= int(pushed);
            if (selStart.y() < -t->sbCount)
                q->clearSelection();
        }
    }
    setScrollPx(scrollPx);
    if (q->hasActiveFocus() && qGuiApp)
        qGuiApp->inputMethod()->update(Qt::ImCursorRectangle);
    q->update();
}

void TerminalItem::Private::inputSent()
{
    q->clearSelection();
    q->scrollToBottom();
}

qreal TerminalItem::Private::maxScrollPx() const
{
    const Terminal::Private *t = td();
    if (!t || !resizes)
        return 0;
    return qreal(t->sbCount) * cellH;
}

void TerminalItem::Private::setScrollPx(qreal px)
{
    const qreal clamped = qBound(qreal(0), px, maxScrollPx());
    if (!qFuzzyCompare(clamped + 1, scrollPx + 1)) {
        scrollPx = clamped;
        q->update();
    }
    scrollPx = clamped;
    updateScrolledBack();
}

void TerminalItem::Private::updateScrolledBack()
{
    const bool back = scrollPx > 0.5;
    if (back != wasScrolledBack) {
        wasScrolledBack = back;
        emit q->scrolledBackChanged();
    }
}

bool TerminalItem::Private::scrollBy(qreal dy)
{
    Terminal::Private *t = td();
    if (!t)
        return false;
    if (t->altScreen) {
        // Full-screen programs have no scrollback: turn the drag into mouse wheel
        // events when the program asked for mouse reports, else into arrow keys
        // (xterm's alternateScroll).
        altAccum += dy;
        while (std::abs(altAccum) >= cellH) {
            const bool up = altAccum > 0;
            altAccum += up ? -cellH : cellH;
            if (t->mouseMode != VTERM_PROP_MOUSE_NONE) {
                const QPoint c = cellAt(lastPos);
                vterm_mouse_move(t->vt, qBound(0, c.y(), t->rows - 1), qBound(0, c.x(), t->cols - 1), VTERM_MOD_NONE);
                vterm_mouse_button(t->vt, up ? 4 : 5, true, VTERM_MOD_NONE);
            } else {
                vterm_keyboard_key(t->vt, up ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
            }
        }
        return true;
    }
    const qreal before = scrollPx;
    setScrollPx(scrollPx + dy);
    return !qFuzzyCompare(before + 1, scrollPx + 1);
}

QPoint TerminalItem::Private::cellAt(const QPointF &pos) const
{
    const Terminal::Private *t = td();
    const int cols = t ? t->cols : 80;
    const int col = qBound(0, int((pos.x() - margin) / cellW), cols - 1);
    const qreal rowF = (pos.y() - margin) / cellH + topRow();
    return QPoint(col, int(std::floor(rowF)));
}

QRectF TerminalItem::Private::cursorRect() const
{
    const Terminal::Private *t = td();
    if (!t)
        return QRectF();
    const qreal y = margin + (t->cursor.row - topRow()) * cellH;
    return QRectF(margin + t->cursor.col * cellW, y, cellW, cellH);
}

// --- gestures --------------------------------------------------------------

void TerminalItem::Private::press(const QPointF &pos)
{
    pressStoppedFlick = flickTimer.isActive();
    stopFlick();
    pressPos = pos;
    lastPos = pos;
    mode = Pending;
    samples.clear();
    samples.append({clock.elapsed(), pos.y()});
    altAccum = 0;
    longPress.start();
}

void TerminalItem::Private::move(const QPointF &pos)
{
    const QPointF delta = pos - pressPos;
    const qreal threshold = qMax(8, qGuiApp ? qGuiApp->styleHints()->startDragDistance() : 10);
    switch (mode) {
    case Pending:
        if (std::abs(delta.y()) > threshold && std::abs(delta.y()) > std::abs(delta.x())) {
            // A clearly vertical drag is ours; anything else is left to the page stack.
            mode = Vertical;
            longPress.stop();
            area->setKeepMouseGrab(true);
            lastPos = pos;
            samples.append({clock.elapsed(), pos.y()});
        } else if (std::abs(delta.x()) > threshold) {
            mode = Horizontal;
            longPress.stop();
        }
        break;
    case Vertical: {
        scrollBy(pos.y() - lastPos.y());
        const qint64 now = clock.elapsed();
        samples.append({now, pos.y()});
        while (samples.size() > 2 && now - samples.first().t > 100)
            samples.removeFirst();
        break;
    }
    case Selecting: {
        // Scroll when dragging the selection to the top/bottom edge.
        if (resizes && pos.y() < margin + cellH)
            scrollBy(cellH / 2);
        else if (resizes && pos.y() > q->height() - margin - cellH && scrollPx > 0)
            scrollBy(-cellH / 2);
        extendSelection(cellAt(pos));
        break;
    }
    default:
        break;
    }
    lastPos = pos;
}

void TerminalItem::Private::release(const QPointF &pos)
{
    longPress.stop();
    const Mode m = mode;
    mode = Idle;
    area->setKeepMouseGrab(false);
    if (m == Pending) {
        if (!pressStoppedFlick)
            tap(pos);
    } else if (m == Vertical) {
        const qint64 now = clock.elapsed();
        flickVelocity = 0;
        if (samples.size() >= 2 && now - samples.last().t < 80) {
            const Sample &a = samples.first();
            const Sample &b = samples.last();
            if (b.t > a.t)
                flickVelocity = (b.y - a.y) * 1000.0 / (b.t - a.t);
        }
        const Terminal::Private *t = td();
        if (std::abs(flickVelocity) > 150 && t && !t->altScreen) {
            flickVelocity = qBound(qreal(-8000), flickVelocity, qreal(8000));
            flickLast = now;
            flickTimer.start();
        }
    }
}

void TerminalItem::Private::cancelGesture()
{
    longPress.stop();
    if (mode == Selecting || mode == Vertical)
        area->setKeepMouseGrab(false);
    mode = Idle;
}

void TerminalItem::Private::flickStep()
{
    const qint64 now = clock.elapsed();
    const qint64 dt = qMax<qint64>(1, now - flickLast);
    flickLast = now;
    const bool moved = scrollBy(flickVelocity * dt / 1000.0);
    flickVelocity *= std::pow(0.998, double(dt));
    if (!moved || std::abs(flickVelocity) < 30)
        stopFlick();
}

void TerminalItem::Private::stopFlick()
{
    flickTimer.stop();
    flickVelocity = 0;
}

void TerminalItem::Private::startSelection()
{
    if (mode != Pending || !term)
        return;
    Terminal::Private *t = td();
    ensureMetrics();
    const QPoint c = cellAt(pressPos);
    if (c.y() >= t->rows || c.y() < -t->sbCount)
        return;
    TermLine line;
    t->line(c.y(), line);
    auto chAt = [&line](int col) { return col < line.cells.size() ? line.cells.at(col).ch : 0u; };
    int from = c.x();
    int to = c.x();
    if (chAt(from) == TermCell::WideTail && from > 0)
        --from;
    if (isWordChar(chAt(from))) {
        while (from > 0 && (isWordChar(chAt(from - 1)) || chAt(from - 1) == TermCell::WideTail))
            --from;
        while (to + 1 < t->cols && (isWordChar(chAt(to + 1)) || chAt(to + 1) == TermCell::WideTail))
            ++to;
    }
    anchorStart = QPoint(from, c.y());
    anchorEnd = QPoint(to, c.y());
    selStart = anchorStart;
    selEnd = anchorEnd;
    selActive = true;
    mode = Selecting;
    area->setKeepMouseGrab(true);
    emit q->selectionChanged();
    q->update();
}

void TerminalItem::Private::extendSelection(const QPoint &cell)
{
    Terminal::Private *t = td();
    if (!t)
        return;
    QPoint c(cell.x(), qBound(-t->sbCount, cell.y(), t->rows - 1));
    auto before = [](const QPoint &a, const QPoint &b) { return a.y() < b.y() || (a.y() == b.y() && a.x() < b.x()); };
    QPoint s, e;
    if (before(c, anchorStart)) {
        s = c;
        e = anchorEnd;
    } else if (before(anchorEnd, c)) {
        s = anchorStart;
        e = c;
    } else {
        s = anchorStart;
        e = anchorEnd;
    }
    if (s != selStart || e != selEnd) {
        selStart = s;
        selEnd = e;
        emit q->selectionChanged();
        q->update();
    }
}

QString TerminalItem::Private::urlAt(const QPoint &cell) const
{
    Terminal::Private *t = td();
    if (!t || cell.y() >= t->rows || cell.y() < -t->sbCount)
        return QString();
    // Collect the logical line (soft-wrapped rows) around the tapped row.
    int first = cell.y();
    TermLine line;
    while (first > -t->sbCount) {
        t->line(first - 1, line);
        if (!line.wrapped)
            break;
        --first;
    }
    QString text;
    int tapIndex = -1;
    for (int row = first; row < t->rows; ++row) {
        t->line(row, line);
        for (int col = 0; col < t->cols; ++col) {
            if (row == cell.y() && col == cell.x())
                tapIndex = text.size();
            const TermCell c = col < line.cells.size() ? line.cells.at(col) : TermCell{0, {}, {}, 0, 1, 0};
            if (c.ch == TermCell::WideTail) {
                continue;
            }
            const uint ch = c.ch ? c.ch : ' ';
            text += QString::fromUcs4(&ch, 1);
        }
        if (!line.wrapped || row >= cell.y() + 4)
            break;
    }
    if (tapIndex < 0)
        return QString();
    static const QRegularExpression re(QStringLiteral("https?://[^\\s<>\"'`\\x{2500}-\\x{259F}]+"),
                                       QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        QString url = m.captured(0);
        // Drop trailing punctuation and unbalanced closing brackets.
        while (!url.isEmpty()) {
            const QChar last = url.at(url.size() - 1);
            if (QStringLiteral(".,;:!?'\"").contains(last)) {
                url.chop(1);
                continue;
            }
            const QString closers = QStringLiteral(")]}>");
            const QString openers = QStringLiteral("([{<");
            const int k = closers.indexOf(last);
            if (k >= 0 && url.count(closers.at(k)) > url.count(openers.at(k))) {
                url.chop(1);
                continue;
            }
            break;
        }
        if (tapIndex >= m.capturedStart(0) && tapIndex < m.capturedStart(0) + url.size())
            return url;
    }
    return QString();
}

void TerminalItem::Private::tap(const QPointF &pos)
{
    if (selActive) {
        q->clearSelection();
        return;
    }
    Terminal::Private *t = td();
    if (t) {
        ensureMetrics();
        const QPoint c = cellAt(pos);
        const QString url = urlAt(c);
        if (!url.isEmpty()) {
            emit q->linkActivated(url);
            return;
        }
        if (t->mouseMode != VTERM_PROP_MOUSE_NONE && scrollPx <= 0 && c.y() >= 0 && c.y() < t->rows) {
            vterm_mouse_move(t->vt, c.y(), c.x(), VTERM_MOD_NONE);
            vterm_mouse_button(t->vt, 1, true, VTERM_MOD_NONE);
            vterm_mouse_button(t->vt, 1, false, VTERM_MOD_NONE);
        }
    }
    emit q->clicked();
}

// --- pinch -----------------------------------------------------------------

bool TerminalItem::Private::pinchTouch(QTouchEvent *e)
{
    const bool ending = e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel;
    if (!pinching) {
        QList<QTouchEvent::TouchPoint> active;
        for (const QTouchEvent::TouchPoint &p : e->touchPoints()) {
            if (p.state() != Qt::TouchPointReleased)
                active.append(p);
        }
        if (ending || active.size() < 2 || !resizes)
            return false;
        for (int k = 0; k < 2; ++k) {
            pinchIds[k] = active.at(k).id();
            pinchPos[k] = active.at(k).pos();
        }
        const QPointF d = pinchPos[0] - pinchPos[1];
        pinchStartDist = qMax(qreal(1), std::hypot(d.x(), d.y()));
        pinchStartFont = fontPx;
        pinching = true;
        longPress.stop();
        stopFlick();
        mode = Idle;
        resizeTimer.stop();
        q->grabTouchPoints(QVector<int>() << pinchIds[0] << pinchIds[1]);
        q->setKeepTouchGrab(true);
        holdMouseGrab();
        if (touchDebugEnabled())
            fprintf(stderr, "TOUCH pinch START dist %.0f font %.0f\n", pinchStartDist, fontPx);
        return true;
    }

    // Follow the two pinch points by id; an event may leave out a point that did not
    // move, and other points (a third finger) are ignored.
    bool released = false;
    for (const QTouchEvent::TouchPoint &p : e->touchPoints()) {
        for (int k = 0; k < 2; ++k) {
            if (p.id() != pinchIds[k])
                continue;
            if (p.state() == Qt::TouchPointReleased)
                released = true;
            else
                pinchPos[k] = p.pos();
        }
    }
    if (ending || released) {
        if (touchDebugEnabled())
            fprintf(stderr, "TOUCH pinch END ending=%d released=%d\n", int(ending), int(released));
        endPinch();
        return true;
    }
    holdMouseGrab();
    const QPointF d = pinchPos[0] - pinchPos[1];
    const qreal dist = std::hypot(d.x(), d.y());
    q->setFontPixelSize(qRound(pinchStartFont * dist / pinchStartDist));
    if (touchDebugEnabled())
        fprintf(stderr, "TOUCH pinch dist %.0f start %.0f font %.0f\n", dist, pinchStartDist, fontPx);
    return true;
}

void TerminalItem::Private::holdMouseGrab()
{
    // Qt 5.6 drops our mouse grab right after the filter takes the touch points, and
    // then offers the first touch point to the ancestors' filters as a synthesized
    // mouse event: Silica's PageStack takes the horizontal finger movement of a pinch
    // as a back swipe and steals the point. A mouse grab with keepMouseGrab makes it
    // leave the pinch alone.
    if (q->window() && q->window()->mouseGrabberItem() != q)
        q->grabMouse();
    q->setKeepMouseGrab(true);
}

void TerminalItem::Private::endPinch()
{
    if (!pinching)
        return;
    pinching = false;
    q->setKeepTouchGrab(false);
    q->setKeepMouseGrab(false);
    if (q->window() && q->window()->mouseGrabberItem() == q)
        q->ungrabMouse();
    scheduleResize();
}

// --- painting --------------------------------------------------------------

bool TerminalItem::Private::selected(int row, int col) const
{
    if (!selActive || row < selStart.y() || row > selEnd.y())
        return false;
    if (row == selStart.y() && col < selStart.x())
        return false;
    if (row == selEnd.y() && col > selEnd.x())
        return false;
    return true;
}

bool TerminalItem::Private::inPrimaryFont(uint ch)
{
    if (ch < 0x7F)
        return true;
    QHash<uint, bool>::const_iterator it = coverage.constFind(ch);
    if (it != coverage.constEnd())
        return it.value();
    const bool in = QFontMetrics(fonts[0]).inFontUcs4(ch);
    coverage.insert(ch, in);
    return in;
}

qreal TerminalItem::Private::advance(int style, const QString &s, uint key)
{
    const quint64 k = (quint64(style) << 32) | key;
    QHash<quint64, qreal>::const_iterator it = advances.constFind(k);
    if (it != advances.constEnd())
        return it.value();
    const qreal a = QFontMetricsF(fonts[style]).width(s);
    if (key)
        advances.insert(k, a);
    return a;
}

void TerminalItem::Private::drawCellText(QPainter *p, const TermLine &line, int col, int combOffset, int x, int w,
                                         int y, QRgb fg, int style)
{
    const TermCell &c = line.cells.at(col);
    if (BoxDrawing::handles(c.ch) && c.ncomb == 0) {
        if (BoxDrawing::draw(p, c.ch, QRect(x, y, w, cellH), QColor(fg), lineW))
            return;
    }
    uint ucs[VTERM_MAX_CHARS_PER_CELL];
    int n = 0;
    ucs[n++] = c.ch;
    for (int j = 0; j < c.ncomb && combOffset + j < line.comb.size(); ++j)
        ucs[n++] = line.comb.at(combOffset + j);
    const QString s = QString::fromUcs4(ucs, n);
    const qreal adv = advance(style, s, c.ncomb ? 0 : c.ch);
    qreal tx = x;
    if (adv < w)
        tx += (w - adv) / 2; // center fallback glyphs / wide characters in their cells
    p->setFont(fonts[style]);
    p->setPen(QColor(fg));
    if (adv > w * 1.05) {
        // Glyph wider than its cells (fallback font): squeeze it in.
        p->save();
        p->translate(x, y + ascent);
        p->scale(w / adv, 1);
        p->drawText(QPointF(0, 0), s);
        p->restore();
    } else {
        p->drawText(QPointF(tx, y + ascent), s);
    }
}

void TerminalItem::Private::drawRow(QPainter *p, const TermLine &line, int row, int y, int cols, int ox)
{
    const int n = qMin(line.cells.size(), cols);
    struct Resolved { QRgb fg; QRgb bg; };
    QVarLengthArray<Resolved, 256> res(qMax(n, 1));
    for (int col = 0; col < n; ++col) {
        const TermCell &c = line.cells.at(col);
        QRgb fg = resolve(c.fg);
        QRgb bg = resolve(c.bg);
        if (c.attrs & TermCell::Reverse)
            qSwap(fg, bg);
        if (c.attrs & TermCell::Faint)
            fg = blend(fg, bg, 0.55);
        res[col].fg = fg;
        res[col].bg = bg;
    }

    // Backgrounds, merged into runs. Selection covers the full width of selected rows.
    int col = 0;
    while (col < cols) {
        const bool sel = selected(row, col);
        const QRgb bg = sel ? kSelection : (col < n ? res[col].bg : kBackground);
        int end = col + 1;
        while (end < cols) {
            const bool s2 = selected(row, end);
            const QRgb b2 = s2 ? kSelection : (end < n ? res[end].bg : kBackground);
            if (b2 != bg)
                break;
            ++end;
        }
        if (bg != kBackground)
            p->fillRect(QRect(xAt(col, ox), y, xAt(end, ox) - xAt(col, ox), cellH), QColor(bg));
        col = end;
    }

    // Text: runs of plain single-width characters with the same color and style go
    // into one drawText; wide, combining, fallback and box-drawing characters are
    // drawn cell by cell.
    QString run;
    int comb = 0;
    col = 0;
    while (col < n) {
        const TermCell &c = line.cells.at(col);
        const int style = ((c.attrs & TermCell::Bold) ? 1 : 0) | ((c.attrs & TermCell::Italic) ? 2 : 0);
        const bool visible = c.ch != 0 && c.ch != ' ' && c.ch != TermCell::WideTail && !(c.attrs & TermCell::Conceal);
        if (!visible) {
            comb += c.ncomb;
            ++col;
            continue;
        }
        const bool simple = c.width == 1 && c.ncomb == 0 && !BoxDrawing::handles(c.ch) && inPrimaryFont(c.ch);
        if (!simple) {
            const int w = xAt(col + qMax(1, int(c.width)), ox) - xAt(col, ox);
            drawCellText(p, line, col, comb, xAt(col, ox), w, y, res[col].fg, style);
            comb += c.ncomb;
            col += qMax(1, int(c.width));
            continue;
        }
        const QRgb fg = res[col].fg;
        const int start = col;
        run.clear();
        while (col < n) {
            const TermCell &d = line.cells.at(col);
            const int st = ((d.attrs & TermCell::Bold) ? 1 : 0) | ((d.attrs & TermCell::Italic) ? 2 : 0);
            const bool blank = (d.ch == 0 || d.ch == ' ' || (d.attrs & TermCell::Conceal)) && d.width == 1;
            if (blank) {
                run += QLatin1Char(' ');
                ++col;
                continue;
            }
            if (d.width != 1 || d.ncomb || st != style || res[col].fg != fg || BoxDrawing::handles(d.ch)
                || !inPrimaryFont(d.ch))
                break;
            if (d.ch < 0x10000) {
                run += QChar(ushort(d.ch));
            } else {
                run += QChar(QChar::highSurrogate(d.ch));
                run += QChar(QChar::lowSurrogate(d.ch));
            }
            ++col;
        }
        // Trailing spaces were added optimistically; they draw nothing.
        int trimmed = run.size();
        while (trimmed > 0 && run.at(trimmed - 1) == QLatin1Char(' '))
            --trimmed;
        run.truncate(trimmed);
        p->setPen(QColor(fg));
        const QRawFont &raw = rawFonts[style];
        if (raw.isValid()) {
            // Glyphs placed on the cell grid directly: no text shaping per frame, and
            // columns stay aligned whatever the font's advances are.
            const QVector<quint32> glyphs = raw.glyphIndexesForString(run);
            glyphPositions.resize(glyphs.size());
            const int x0 = xAt(start, ox);
            for (int i = 0; i < glyphs.size(); ++i)
                glyphPositions[i] = QPointF(xAt(start + i, ox) - x0, 0);
            QGlyphRun gr;
            gr.setRawFont(raw);
            gr.setGlyphIndexes(glyphs);
            gr.setPositions(glyphPositions);
            p->drawGlyphRun(QPointF(x0, y + ascent), gr);
        } else {
            p->setFont(fonts[style]);
            p->drawText(QPointF(xAt(start, ox), y + ascent), run);
        }
    }

    // Underline and strikethrough.
    for (col = 0; col < n;) {
        const TermCell &c = line.cells.at(col);
        const int ul = c.underline();
        const bool strike = c.attrs & TermCell::Strike;
        if (!ul && !strike) {
            ++col;
            continue;
        }
        const int start = col;
        const QRgb fg = res[col].fg;
        while (col < n && line.cells.at(col).underline() == ul
               && bool(line.cells.at(col).attrs & TermCell::Strike) == strike && res[col].fg == fg)
            ++col;
        const int x0 = xAt(start, ox);
        const int x1 = xAt(col, ox);
        const QColor color(fg);
        if (ul) {
            const int uy = y + qMin(qRound(ascent + underlinePos), cellH - lineW);
            p->fillRect(QRect(x0, uy, x1 - x0, lineW), color);
            if (ul == VTERM_UNDERLINE_DOUBLE)
                p->fillRect(QRect(x0, qMin(uy + 2 * lineW, y + cellH - lineW), x1 - x0, lineW), color);
            else if (ul == VTERM_UNDERLINE_CURLY)
                p->fillRect(QRect(x0, qMax(y, uy - lineW), x1 - x0, lineW), color);
        }
        if (strike)
            p->fillRect(QRect(x0, y + qRound(ascent - strikePos) - lineW / 2, x1 - x0, lineW), color);
    }
}

// ---------------------------------------------------------------------------
// TerminalItem

TerminalItem::TerminalItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , d(new Private(this))
{
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setPerformanceHint(QQuickPaintedItem::FastFBOResizing, true);
    setOpaquePainting(true);
    setFillColor(QColor(kBackground));
    setAntialiasing(false);
    setFlag(ItemAcceptsInputMethod, true);
    setFlag(ItemHasContents, true);
    setFiltersChildMouseEvents(true);
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    setAcceptTouchEvents(true); // needed for filtering touch events since 5.10
#endif
    d->area = new Private::TouchArea(d, this);
}

TerminalItem::~TerminalItem()
{
    // The touch area calls into d (e.g. on mouse ungrab); delete it first.
    delete d->area;
    d->area = nullptr;
    delete d;
}

Terminal *TerminalItem::terminal() const
{
    return d->term;
}

void TerminalItem::setTerminal(Terminal *terminal)
{
    if (d->term == terminal)
        return;
    if (d->term)
        disconnect(d->term, nullptr, this, nullptr);
    d->term = terminal;
    d->scrollPx = 0;
    d->selActive = false;
    d->preedit.clear();
    if (terminal) {
        d->seenPushed = terminal->d->sbPushed;
        d->seenReset = terminal->d->sbReset;
        connect(terminal, &Terminal::contentChanged, this, [this]() { d->contentChanged(); });
        connect(terminal, &Terminal::inputSent, this, [this]() { d->inputSent(); });
        connect(terminal, &Terminal::sizeChanged, this, [this]() {
            if (d->selActive)
                clearSelection();
            if (!d->resizes)
                d->invalidateMetrics();
            d->setScrollPx(d->scrollPx);
            update();
        });
        connect(terminal, &QObject::destroyed, this, [this]() { update(); });
    }
    d->invalidateMetrics();
    d->everResized = false;
    d->scheduleResize();
    d->updateScrolledBack();
    emit terminalChanged();
    emit selectionChanged();
    update();
}

qreal TerminalItem::fontPixelSize() const
{
    return d->fontPx;
}

void TerminalItem::setFontPixelSize(qreal size)
{
    size = qBound(kMinFontPx, qreal(qRound(size)), kMaxFontPx);
    if (qFuzzyCompare(size, d->fontPx))
        return;
    d->fontPx = size;
    d->invalidateMetrics();
    d->ensureMetrics();
    d->setScrollPx(d->scrollPx);
    d->scheduleResize();
    emit fontPixelSizeChanged();
    update();
}

QString TerminalItem::fontFamily() const
{
    return d->family;
}

void TerminalItem::setFontFamily(const QString &family)
{
    const QString f = family.isEmpty() ? QStringLiteral("DejaVu Sans Mono") : family;
    if (f == d->family)
        return;
    d->family = f;
    d->coverage.clear();
    d->invalidateMetrics();
    d->ensureMetrics();
    d->scheduleResize();
    emit fontFamilyChanged();
    update();
}

bool TerminalItem::resizesTerminal() const
{
    return d->resizes;
}

void TerminalItem::setResizesTerminal(bool on)
{
    if (d->resizes == on)
        return;
    d->resizes = on;
    d->scrollPx = 0;
    d->updateScrolledBack();
    d->invalidateMetrics();
    d->scheduleResize();
    emit resizesTerminalChanged();
    update();
}

qreal TerminalItem::margin() const
{
    return d->margin;
}

void TerminalItem::setMargin(qreal margin)
{
    margin = qMax(qreal(0), margin);
    if (qFuzzyCompare(margin + 1, d->margin + 1))
        return;
    d->margin = margin;
    d->invalidateMetrics();
    d->scheduleResize();
    emit marginChanged();
    update();
}

bool TerminalItem::hasSelection() const
{
    return d->selActive;
}

QString TerminalItem::selectedText() const
{
    Terminal::Private *t = d->td();
    if (!d->selActive || !t)
        return QString();
    QString out;
    TermLine line;
    for (int row = d->selStart.y(); row <= d->selEnd.y(); ++row) {
        t->line(row, line);
        const int from = row == d->selStart.y() ? d->selStart.x() : 0;
        const int to = row == d->selEnd.y() ? d->selEnd.x() + 1 : t->cols;
        QString s = t->lineText(line, from, to);
        const bool last = row == d->selEnd.y();
        if (!(line.wrapped && !last)) {
            int end = s.size();
            while (end > 0 && s.at(end - 1) == QLatin1Char(' '))
                --end;
            s.truncate(end);
        }
        out += s;
        if (!last && !line.wrapped)
            out += QLatin1Char('\n');
    }
    return out;
}

bool TerminalItem::scrolledBack() const
{
    return d->scrollPx > 0.5;
}

qreal TerminalItem::cellWidth() const
{
    d->ensureMetrics();
    return d->cellW;
}

qreal TerminalItem::cellHeight() const
{
    d->ensureMetrics();
    return d->cellH;
}

void TerminalItem::copySelection()
{
    const QString text = selectedText();
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
    clearSelection();
}

void TerminalItem::clearSelection()
{
    if (!d->selActive)
        return;
    d->selActive = false;
    if (d->mode == Private::Selecting)
        d->cancelGesture();
    emit selectionChanged();
    update();
}

void TerminalItem::pasteClipboard()
{
    if (!d->term)
        return;
    const QString text = QGuiApplication::clipboard()->text();
    if (!text.isEmpty())
        d->term->paste(text); // emits inputSent: clears selection, jumps to the bottom
}

void TerminalItem::scrollToBottom()
{
    d->stopFlick();
    d->setScrollPx(0);
}

void TerminalItem::paint(QPainter *painter)
{
    painter->fillRect(QRectF(0, 0, width(), height()), QColor(kBackground));
    Terminal::Private *t = d->td();
    if (!t)
        return;
    d->ensureMetrics();
    const int ox = qRound(d->margin);
    const int oy = qRound(d->margin);
    const qreal top = d->topRow();
    const int first = int(std::floor(top));
    const int yoff = qRound((first - top) * d->cellH);
    const int visible = int(std::ceil((height() - oy) / d->cellH)) + 1;
    TermLine line;
    for (int i = 0; i <= visible; ++i) {
        const int row = first + i;
        if (row >= t->rows)
            break;
        if (row < -t->sbCount)
            continue;
        t->line(row, line);
        d->drawRow(painter, line, row, oy + yoff + i * d->cellH, t->cols, ox);
    }

    // Cursor
    const int crow = t->cursor.row;
    const int ccol = qBound(0, t->cursor.col, t->cols - 1);
    const int cy = oy + yoff + (crow - first) * d->cellH;
    if (t->cursorVisible && cy + d->cellH > 0 && cy < height()) {
        t->line(crow, line);
        int width = 1;
        if (ccol < line.cells.size() && line.cells.at(ccol).width == 2)
            width = 2;
        const int x0 = d->xAt(ccol, ox);
        const QRect rect(x0, cy, d->xAt(ccol + width, ox) - x0, d->cellH);
        const QColor cursorColor(kCursor);
        const bool focused = hasActiveFocus();
        const int lw = qMax(1, d->lineW);
        if (!focused) {
            painter->setPen(QPen(cursorColor, lw));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(QRectF(rect).adjusted(lw / 2.0, lw / 2.0, -lw / 2.0, -lw / 2.0));
        } else if (t->cursorShape == VTERM_PROP_CURSORSHAPE_UNDERLINE) {
            painter->fillRect(QRect(rect.x(), rect.bottom() + 1 - 2 * lw, rect.width(), 2 * lw), cursorColor);
        } else if (t->cursorShape == VTERM_PROP_CURSORSHAPE_BAR_LEFT) {
            painter->fillRect(QRect(rect.x(), rect.y(), 2 * lw, rect.height()), cursorColor);
        } else {
            painter->fillRect(rect, cursorColor);
            if (ccol < line.cells.size()) {
                const TermCell &c = line.cells.at(ccol);
                if (c.ch && c.ch != ' ' && c.ch != TermCell::WideTail && !(c.attrs & TermCell::Conceal)) {
                    int comb = 0;
                    for (int i = 0; i < ccol; ++i)
                        comb += line.cells.at(i).ncomb;
                    const int style = ((c.attrs & TermCell::Bold) ? 1 : 0) | ((c.attrs & TermCell::Italic) ? 2 : 0);
                    d->drawCellText(painter, line, ccol, comb, rect.x(), rect.width(), cy, kBackground, style);
                }
            }
        }
    }

    // Input method pre-edit text (not sent until committed).
    if (!d->preedit.isEmpty()) {
        const int x0 = d->xAt(ccol, ox);
        const qreal w = QFontMetricsF(d->fonts[0]).width(d->preedit);
        painter->fillRect(QRectF(x0, cy, w, d->cellH), QColor(kBackground));
        painter->setFont(d->fonts[0]);
        painter->setPen(QColor(kForeground));
        painter->drawText(QPointF(x0, cy + d->ascent), d->preedit);
        painter->fillRect(QRectF(x0, cy + d->cellH - d->lineW, w, d->lineW), QColor(kCursor));
    }
}

void TerminalItem::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChanged(newGeometry, oldGeometry);
    d->area->setSize(newGeometry.size());
    if (newGeometry.size() != oldGeometry.size()) {
        if (!d->resizes)
            d->invalidateMetrics();
        d->scheduleResize();
        update();
    }
}

void TerminalItem::componentComplete()
{
    QQuickPaintedItem::componentComplete();
    d->scheduleResize();
}

QVariant TerminalItem::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImHints:
        // No prediction, auto-capitals or word learning: keystrokes go straight to the
        // shell, and passwords typed at prompts are not remembered by the keyboard.
        return int(Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase | Qt::ImhPreferLowercase
                   | Qt::ImhSensitiveData);
    case Qt::ImCursorRectangle:
#if QT_VERSION >= QT_VERSION_CHECK(5, 7, 0)
    case Qt::ImAnchorRectangle:
#endif
        d->ensureMetrics();
        return d->cursorRect();
    case Qt::ImFont:
        d->ensureMetrics();
        return d->fonts[0];
    case Qt::ImCursorPosition:
    case Qt::ImAnchorPosition:
        return 0;
    case Qt::ImSurroundingText:
    case Qt::ImCurrentSelection:
    case Qt::ImTextBeforeCursor:
    case Qt::ImTextAfterCursor:
        return QString();
    case Qt::ImEnterKeyType:
        return int(Qt::EnterKeyDefault);
    default:
        return QQuickPaintedItem::inputMethodQuery(query);
    }
}

void TerminalItem::inputMethodEvent(QInputMethodEvent *event)
{
    Terminal *t = d->term;
    if (t) {
        // A replacement reaching back before the cursor = the keyboard deleting text it
        // thinks is there; send it as backspaces.
        if (event->replacementLength() > 0 && event->replacementStart() < 0) {
            const int n = qMin(event->replacementLength(), -event->replacementStart());
            for (int i = 0; i < n; ++i)
                t->d->sendKey(VTERM_KEY_BACKSPACE, VTERM_MOD_NONE);
        }
        if (!event->commitString().isEmpty())
            t->sendText(event->commitString()); // honors the one-shot Ctrl/Alt
    }
    if (d->preedit != event->preeditString()) {
        d->preedit = event->preeditString();
        update();
    }
    event->accept();
}

void TerminalItem::keyPressEvent(QKeyEvent *event)
{
    Terminal *t = d->term;
    if (!t) {
        event->ignore();
        return;
    }
    Terminal::Private *td = t->d;
    const Qt::KeyboardModifiers m = event->modifiers();
    const bool ctrl = (m & Qt::ControlModifier) || t->ctrlModifier();
    const bool alt = (m & Qt::AltModifier) || t->altModifier();
    const bool shift = m & Qt::ShiftModifier;
    const int key = event->key();

    // Local shortcuts (hardware keyboards).
    if ((m & Qt::ControlModifier) && shift && key == Qt::Key_V) {
        pasteClipboard();
        return;
    }
    if ((m & Qt::ControlModifier) && shift && key == Qt::Key_C) {
        copySelection();
        return;
    }
    if (shift && !(m & Qt::ControlModifier) && (key == Qt::Key_PageUp || key == Qt::Key_PageDown) && !td->altScreen) {
        d->ensureMetrics();
        const qreal page = (d->contentRows() - 1) * d->cellH;
        d->setScrollPx(d->scrollPx + (key == Qt::Key_PageUp ? page : -page));
        return;
    }

    int mod = VTERM_MOD_NONE;
    if (ctrl)
        mod |= VTERM_MOD_CTRL;
    if (alt)
        mod |= VTERM_MOD_ALT;
    if (shift)
        mod |= VTERM_MOD_SHIFT;

    VTermKey vkey = VTERM_KEY_NONE;
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter: vkey = VTERM_KEY_ENTER; break;
    case Qt::Key_Backspace: vkey = VTERM_KEY_BACKSPACE; break;
    case Qt::Key_Tab: vkey = VTERM_KEY_TAB; break;
    case Qt::Key_Backtab: vkey = VTERM_KEY_TAB; mod |= VTERM_MOD_SHIFT; break;
    case Qt::Key_Escape: vkey = VTERM_KEY_ESCAPE; break;
    case Qt::Key_Up: vkey = VTERM_KEY_UP; break;
    case Qt::Key_Down: vkey = VTERM_KEY_DOWN; break;
    case Qt::Key_Left: vkey = VTERM_KEY_LEFT; break;
    case Qt::Key_Right: vkey = VTERM_KEY_RIGHT; break;
    case Qt::Key_Home: vkey = VTERM_KEY_HOME; break;
    case Qt::Key_End: vkey = VTERM_KEY_END; break;
    case Qt::Key_PageUp: vkey = VTERM_KEY_PAGEUP; break;
    case Qt::Key_PageDown: vkey = VTERM_KEY_PAGEDOWN; break;
    case Qt::Key_Insert: vkey = VTERM_KEY_INS; break;
    case Qt::Key_Delete: vkey = VTERM_KEY_DEL; break;
    default:
        if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
            vkey = VTermKey(VTERM_KEY_FUNCTION(key - Qt::Key_F1 + 1));
        break;
    }

    if (vkey != VTERM_KEY_NONE) {
        td->consumeModifiers();
        td->sendKey(vkey, VTermModifier(mod));
        event->accept();
        return;
    }

    const QString text = event->text();
    if (ctrl) {
        // Qt key codes of printable ASCII keys are the (upper case) characters.
        uint c = 0;
        if (key >= 0x20 && key < 0x7f)
            c = uint(key);
        else if (!text.isEmpty() && text.at(0).unicode() >= 0x20)
            c = text.at(0).unicode();
        if (c) {
            td->consumeModifiers();
            td->sendText(QString(QChar(ushort(c))), true, alt);
            event->accept();
            return;
        }
    }
    if (!text.isEmpty()) {
        const ushort c0 = text.at(0).unicode();
        td->consumeModifiers();
        if (c0 < 0x20 || c0 == 0x7f) {
            // Already a control character (e.g. Ctrl+[ from some keyboards).
            td->sendBytes((alt ? QByteArray("\x1b") : QByteArray()) + text.toLatin1());
            td->noteInput();
        } else {
            td->sendText(text, false, alt);
        }
        event->accept();
        return;
    }
    event->ignore();
}

void TerminalItem::focusInEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusInEvent(event);
    if (d->td())
        vterm_state_focus_in(d->td()->state);
    update();
}

void TerminalItem::focusOutEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusOutEvent(event);
    if (d->td())
        vterm_state_focus_out(d->td()->state);
    if (!d->preedit.isEmpty()) {
        d->preedit.clear();
    }
    update();
}

void TerminalItem::wheelEvent(QWheelEvent *event)
{
    d->ensureMetrics();
    const int dy = event->angleDelta().y();
    if (event->modifiers() & Qt::ControlModifier) {
        if (dy)
            setFontPixelSize(d->fontPx + (dy > 0 ? 1 : -1));
        event->accept();
        return;
    }
    if (!event->pixelDelta().isNull())
        d->scrollBy(event->pixelDelta().y());
    else
        d->scrollBy(dy / 120.0 * 3 * d->cellH);
    event->accept();
}


static void logTouch(const char *where, QEvent *event, QQuickItem *self)
{
    if (!touchDebugEnabled())
        return;
    QString s;
    if (event && (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate
                  || event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel)) {
        for (const QTouchEvent::TouchPoint &p : static_cast<QTouchEvent *>(event)->touchPoints())
            s += QStringLiteral(" [%1 s%2 %3,%4]").arg(p.id()).arg(int(p.state())).arg(p.pos().x()).arg(p.pos().y());
    } else if (event) {
        const QMouseEvent *m = dynamic_cast<QMouseEvent *>(event);
        if (m)
            s = QStringLiteral(" mouse %1,%2 src%3").arg(m->localPos().x()).arg(m->localPos().y()).arg(int(m->source()));
    }
    QQuickItem *grabber = self->window() ? self->window()->mouseGrabberItem() : nullptr;
    fprintf(stderr, "TOUCH %lld %s type=%d%s grabber=%s\n", (long long)QDateTime::currentMSecsSinceEpoch() % 100000, where,
            event ? int(event->type()) : -1, qPrintable(s), grabber ? grabber->metaObject()->className() : "none");
}

bool TerminalItem::childMouseEventFilter(QQuickItem *item, QEvent *event)
{
    Q_UNUSED(item)
    logTouch("filter", event, this);
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        return d->pinchTouch(static_cast<QTouchEvent *>(event));
    default:
        return false;
    }
}

void TerminalItem::touchEvent(QTouchEvent *event)
{
    logTouch("touchEvent", event, this);
    // Only reached for the touch points grabbed for a pinch.
    if (d->pinching) {
        d->pinchTouch(event);
        event->accept();
    } else {
        event->ignore();
    }
}

void TerminalItem::touchUngrabEvent()
{
    logTouch("touchUngrab", nullptr, this);
    d->endPinch();
}
