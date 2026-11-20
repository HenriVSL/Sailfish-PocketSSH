#include "terminal/terminal.h"
#include "terminal/terminal_p.h"
#include "terminal/vtermhooks.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>

#include <cstring>

namespace {

// Parse at most this long per event-loop pass before yielding to the UI; more when a
// large backlog has built up, so memory stays bounded when output arrives faster than
// it can be parsed.
const qint64 kPassBudgetNs = 12 * 1000 * 1000;
const qint64 kBacklogPassBudgetNs = 40 * 1000 * 1000;
const int kBacklogBytes = 4 * 1024 * 1024;
// Bytes handed to libvterm between budget checks.
const int kChunk = 4096;
// Longest OSC 52 payload we accept (decoded bytes) and longest title.
const int kMaxClipboard = 1024 * 1024;
const int kMaxTitle = 4096;

// Control character for Ctrl+<c>, or -1.
int controlCode(uint c)
{
    if (c >= 'a' && c <= 'z')
        return int(c - 'a' + 1);
    if (c >= '@' && c <= '_') // @ A-Z [ \ ] ^ _
        return int(c - '@');
    switch (c) {
    case ' ':
    case '2':
        return 0;
    case '3':
        return 0x1b;
    case '4':
        return 0x1c;
    case '5':
        return 0x1d;
    case '6':
        return 0x1e;
    case '7':
    case '-':
    case '/':
        return 0x1f;
    case '?':
    case '8':
        return 0x7f;
    }
    return -1;
}

struct KeyName {
    const char *name;
    VTermKey key;
};

const KeyName kKeyNames[] = {
    {"enter", VTERM_KEY_ENTER},     {"return", VTERM_KEY_ENTER},  {"backspace", VTERM_KEY_BACKSPACE},
    {"tab", VTERM_KEY_TAB},         {"esc", VTERM_KEY_ESCAPE},    {"escape", VTERM_KEY_ESCAPE},
    {"up", VTERM_KEY_UP},           {"down", VTERM_KEY_DOWN},     {"left", VTERM_KEY_LEFT},
    {"right", VTERM_KEY_RIGHT},     {"home", VTERM_KEY_HOME},     {"end", VTERM_KEY_END},
    {"pgup", VTERM_KEY_PAGEUP},     {"pageup", VTERM_KEY_PAGEUP}, {"pgdn", VTERM_KEY_PAGEDOWN},
    {"pagedown", VTERM_KEY_PAGEDOWN}, {"insert", VTERM_KEY_INS},  {"delete", VTERM_KEY_DEL},
    {"del", VTERM_KEY_DEL},
};

} // namespace

// ---------------------------------------------------------------------------
// Private

Terminal::Private::Private(Terminal *q)
    : q(q)
{
    passTimer = new QTimer(q);
    passTimer->setSingleShot(true);
    passTimer->setInterval(0);
    QObject::connect(passTimer, &QTimer::timeout, q, [this]() { endPass(); });

    vt = vterm_new(rows, cols);
    vterm_set_utf8(vt, 1);
    vterm_output_set_callback(vt, &Private::outputCb, this);

    screen = vterm_obtain_screen(vt);
    state = vterm_obtain_state(vt);
    pocketssh_vterm_install_hooks(vt);

    static const VTermScreenCallbacks screenCallbacks = {
        nullptr, // damage: views redraw everything per frame
        nullptr, // moverect
        &Private::moveCursorCb,
        &Private::setTermPropCb,
        &Private::bellCb,
        &Private::resizeCb,
        &Private::sbPushLineCb,
        &Private::sbPopLineCb,
        &Private::sbClearCb,
    };
    static const VTermSelectionCallbacks selectionCallbacks = {
        &Private::selectionSetCb,
        &Private::selectionQueryCb,
    };
    vterm_screen_set_callbacks(screen, &screenCallbacks, this);
    vterm_screen_enable_altscreen(screen, 1);
    vterm_screen_enable_reflow(screen, true);
    // Merge damage per scroll: we have no damage callback (views repaint whole frames),
    // and this mode makes libvterm skip a second pass over scrolled rectangles.
    vterm_screen_set_damage_merge(screen, VTERM_DAMAGE_SCROLL);
    vterm_state_set_bold_highbright(state, 1);
    vterm_state_set_selection_callbacks(state, &selectionCallbacks, this, selectionBuffer,
                                        sizeof(selectionBuffer));
    vterm_screen_reset(screen, 1);

    sb.resize(sbCapacity);

}

Terminal::Private::~Private()
{
    vterm_free(vt);
}

void Terminal::Private::cellFromVTerm(const VTermScreenCell &src, TermCell &dst, QVector<quint32> &comb)
{
    dst.ch = src.chars[0];
    dst.ncomb = 0;
    if (dst.ch != TermCell::WideTail && dst.ch != 0) {
        for (int i = 1; i < VTERM_MAX_CHARS_PER_CELL && src.chars[i]; ++i) {
            comb.append(src.chars[i]);
            ++dst.ncomb;
        }
    }
    dst.fg = src.fg;
    dst.bg = src.bg;
    dst.width = qint8(src.width);
    quint16 a = 0;
    if (src.attrs.bold)
        a |= TermCell::Bold;
    a |= quint16((src.attrs.underline & 3) << 1);
    if (src.attrs.italic)
        a |= TermCell::Italic;
    if (src.attrs.blink)
        a |= TermCell::Blink;
    if (src.attrs.reverse)
        a |= TermCell::Reverse;
    if (src.attrs.conceal)
        a |= TermCell::Conceal;
    if (src.attrs.strike)
        a |= TermCell::Strike;
    if (src.attrs.font == 1) // SGR 2, see vtermhooks.h
        a |= TermCell::Faint;
    dst.attrs = a;
}

void Terminal::Private::line(int row, TermLine &out) const
{
    out.comb.clear();
    out.wrapped = false;
    if (row >= 0) {
        if (row >= rows) {
            out.cells.clear();
            return;
        }
        out.cells.resize(cols);
        VTermScreenCell cell;
        VTermPos pos;
        pos.row = row;
        for (pos.col = 0; pos.col < cols; ++pos.col) {
            std::memset(&cell, 0, sizeof(cell));
            vterm_screen_get_cell(screen, pos, &cell);
            cellFromVTerm(cell, out.cells[pos.col], out.comb);
        }
        if (row + 1 < rows) {
            const VTermLineInfo *info = vterm_state_get_lineinfo(state, row + 1);
            out.wrapped = info && info->continuation;
        }
        return;
    }
    const int idx = sbCount + row;
    if (idx < 0) {
        out.cells.clear();
        return;
    }
    out = sb.at((sbHead + idx) % sbCapacity);
}

QString Terminal::Private::lineText(const TermLine &line, int from, int to) const
{
    const int n = line.cells.size();
    if (to < 0 || to > n)
        to = n;
    QString s;
    s.reserve(qMax(0, to - from));
    int comb = 0;
    for (int i = 0; i < from && i < n; ++i)
        comb += line.cells.at(i).ncomb;
    for (int i = qMax(0, from); i < to; ++i) {
        const TermCell &c = line.cells.at(i);
        if (c.ch == TermCell::WideTail)
            continue;
        if (c.ch == 0) {
            s += QLatin1Char(' ');
        } else {
            uint ucs[VTERM_MAX_CHARS_PER_CELL];
            int k = 0;
            ucs[k++] = c.ch;
            for (int j = 0; j < c.ncomb && comb + j < line.comb.size(); ++j)
                ucs[k++] = line.comb.at(comb + j);
            s += QString::fromUcs4(ucs, k);
        }
        comb += c.ncomb;
    }
    return s;
}

void Terminal::Private::writeInput(const char *data, int len)
{
    vterm_input_write(vt, data, size_t(len));
}

void Terminal::Private::process()
{
    QElapsedTimer t;
    t.start();
    const qint64 budget = pending.size() - pendingOffset > kBacklogBytes ? kBacklogPassBudgetNs : kPassBudgetNs;
    while (pendingOffset < pending.size() && passNs < budget) {
        const int n = qMin(pending.size() - pendingOffset, kChunk);
        // Keep a reference: an output() slot could call feed() re-entrantly, which
        // appends to `pending` and may reallocate it.
        const QByteArray chunk = pending;
        writeInput(chunk.constData() + pendingOffset, n);
        vterm_screen_flush_damage(screen);
        pendingOffset += n;
        passNs += t.nsecsElapsed();
        t.restart();
    }
    if (pendingOffset >= pending.size()) {
        pending.clear();
        pendingOffset = 0;
    } else if (pendingOffset > 1024 * 1024) {
        pending.remove(0, pendingOffset);
        pendingOffset = 0;
    }
    markDirty();
    if (!pending.isEmpty())
        schedulePass();
}

void Terminal::Private::schedulePass()
{
    if (!passTimer->isActive())
        passTimer->start();
}

void Terminal::Private::markDirty()
{
    dirty = true;
    schedulePass();
}

void Terminal::Private::endPass()
{
    passNs = 0;
    if (!pending.isEmpty())
        process(); // reschedules itself while input remains
    if (dirty) {
        dirty = false;
        emit q->contentChanged();
    }
}

bool Terminal::Private::reflowSafe(int newCols, int newRows) const
{
    // libvterm 0.3.3 aborts() in resize_buffer() when the cursor sits on the last row
    // of a soft-wrapped line that no longer fits on the screen after rewrapping. Check
    // for that (conservatively) and resize without reflow in that case.
    VTermPos cur;
    vterm_state_get_cursorpos(state, &cur);
    if (cur.row < 0 || cur.row >= rows)
        return false;
    auto continued = [this](int row) {
        const VTermLineInfo *info = vterm_state_get_lineinfo(state, row);
        return info && info->continuation;
    };
    int top = cur.row;
    while (top > 0 && continued(top))
        --top;
    int bottom = cur.row;
    while (bottom + 1 < rows && continued(bottom + 1))
        ++bottom;
    if (top == bottom)
        return true;
    int last = bottom;
    for (int r = rows - 1; r > bottom; --r) {
        VTermPos p;
        p.row = r;
        p.col = 0;
        if (!vterm_screen_is_eol(screen, p)) {
            last = r;
            break;
        }
    }
    const int growth = (cols + newCols - 1) / newCols;
    return qint64(last - top + 1) * growth < newRows;
}

void Terminal::Private::resize(int newCols, int newRows)
{
    newCols = qBound(2, newCols, 1000);
    newRows = qBound(1, newRows, 500);
    if (newCols == cols && newRows == rows)
        return;
    const bool safe = reflowSafe(newCols, newRows);
    if (!safe)
        vterm_screen_enable_reflow(screen, false);
    vterm_set_size(vt, newRows, newCols);
    if (!safe)
        vterm_screen_enable_reflow(screen, true);
    vterm_screen_flush_damage(screen);
    vterm_state_get_cursorpos(state, &cursor);
    markDirty();
    emit q->sizeChanged();
}

void Terminal::Private::sendBytes(const QByteArray &bytes)
{
    if (!bytes.isEmpty())
        emit q->output(bytes);
}

void Terminal::Private::noteInput()
{
    ++inputSerial;
    emit q->inputSent();
}

void Terminal::Private::consumeModifiers()
{
    if (ctrl) {
        ctrl = false;
        emit q->ctrlModifierChanged();
    }
    if (alt) {
        alt = false;
        emit q->altModifierChanged();
    }
}

void Terminal::Private::sendText(const QString &text, bool withCtrl, bool withAlt)
{
    if (text.isEmpty())
        return;
    const QVector<uint> ucs = text.toUcs4();
    if (withCtrl && ucs.size() == 1) {
        const int code = controlCode(ucs.at(0));
        if (code >= 0) {
            QByteArray b;
            if (withAlt)
                b += '\x1b';
            b += char(code);
            sendBytes(b);
            noteInput();
            return;
        }
    }
    QByteArray out;
    bool first = true;
    for (int i = 0; i < ucs.size(); ++i) {
        const uint c = ucs.at(i);
        if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < ucs.size() && ucs.at(i + 1) == '\n')
                ++i;
            sendBytes(out);
            out.clear();
            vterm_keyboard_key(vt, VTERM_KEY_ENTER, (withAlt && first) ? VTERM_MOD_ALT : VTERM_MOD_NONE);
        } else {
            if (withAlt && first)
                out += '\x1b';
            out += QString::fromUcs4(&c, 1).toUtf8();
        }
        first = false;
    }
    sendBytes(out);
    noteInput();
}

void Terminal::Private::sendKey(VTermKey key, VTermModifier mod)
{
    int m = mod;
    switch (key) {
    case VTERM_KEY_BACKSPACE:
        if (m & VTERM_MOD_CTRL) { // ^H like xterm, instead of libvterm's CSI u form
            sendBytes((m & VTERM_MOD_ALT) ? QByteArray("\x1b\x08") : QByteArray("\x08"));
            noteInput();
            return;
        }
        m &= ~VTERM_MOD_SHIFT;
        break;
    case VTERM_KEY_ENTER:
    case VTERM_KEY_ESCAPE:
        m &= ~(VTERM_MOD_CTRL | VTERM_MOD_SHIFT);
        break;
    case VTERM_KEY_TAB:
        m &= ~VTERM_MOD_CTRL;
        break;
    default:
        break;
    }
    vterm_keyboard_key(vt, key, VTermModifier(m));
    noteInput();
}

void Terminal::Private::pushLine(int ncols, const VTermScreenCell *cells)
{
    // While the alternate screen is active, libvterm 0.3.3 pushes rows read from the
    // alternate buffer when resizing; full-screen apps should not fill scrollback.
    if (altScreen)
        return;
    TermLine &l = sb[(sbHead + (sbCount < sbCapacity ? sbCount : 0)) % sbCapacity];
    if (sbCount < sbCapacity)
        ++sbCount;
    else
        sbHead = (sbHead + 1) % sbCapacity;
    // Store the line without its trailing blank cells.
    int len = ncols;
    while (len > 0) {
        const VTermScreenCell &c = cells[len - 1];
        if ((c.chars[0] != 0 && c.chars[0] != ' ') || !VTERM_COLOR_IS_DEFAULT_BG(&c.bg) || c.attrs.reverse
            || c.attrs.underline || c.attrs.strike)
            break;
        --len;
    }
    l.cells.resize(len);
    l.comb.clear();
    for (int i = 0; i < len; ++i)
        cellFromVTerm(cells[i], l.cells[i], l.comb);
    const VTermLineInfo *info = vterm_state_get_lineinfo(state, 0);
    l.wrapped = info && info->continuation;
    ++sbPushed;
}

bool Terminal::Private::popLine(int ncols, VTermScreenCell *cells)
{
    if (altScreen || sbCount == 0)
        return false;
    const int idx = (sbHead + sbCount - 1) % sbCapacity;
    TermLine l = sb[idx];
    sb[idx] = TermLine();
    --sbCount;
    ++sbReset;

    VTermColor dfg, dbg;
    vterm_state_get_default_colors(state, &dfg, &dbg);
    int comb = 0;
    for (int i = 0; i < ncols; ++i) {
        VTermScreenCell &c = cells[i];
        std::memset(&c, 0, sizeof(c));
        if (i < l.cells.size()) {
            const TermCell &t = l.cells.at(i);
            c.chars[0] = t.ch == TermCell::WideTail ? 0 : t.ch;
            for (int j = 0; j < t.ncomb && j + 1 < VTERM_MAX_CHARS_PER_CELL; ++j)
                c.chars[j + 1] = l.comb.value(comb + j);
            comb += t.ncomb;
            c.width = char(t.width);
            c.fg = t.fg;
            c.bg = t.bg;
            c.attrs.bold = (t.attrs & TermCell::Bold) ? 1 : 0;
            c.attrs.underline = t.underline();
            c.attrs.italic = (t.attrs & TermCell::Italic) ? 1 : 0;
            c.attrs.blink = (t.attrs & TermCell::Blink) ? 1 : 0;
            c.attrs.reverse = (t.attrs & TermCell::Reverse) ? 1 : 0;
            c.attrs.conceal = (t.attrs & TermCell::Conceal) ? 1 : 0;
            c.attrs.strike = (t.attrs & TermCell::Strike) ? 1 : 0;
            c.attrs.font = (t.attrs & TermCell::Faint) ? 1 : 0;
        } else {
            c.width = 1;
            c.fg = dfg;
            c.bg = dbg;
        }
    }
    return true;
}

void Terminal::Private::clearScrollback()
{
    for (int i = 0; i < sbCount; ++i)
        sb[(sbHead + i) % sbCapacity] = TermLine();
    sbHead = 0;
    sbCount = 0;
    ++sbReset;
    markDirty();
}

// --- libvterm callbacks ----------------------------------------------------

void Terminal::Private::outputCb(const char *s, size_t len, void *user)
{
    Private *d = static_cast<Private *>(user);
    emit d->q->output(QByteArray(s, int(len)));
}

int Terminal::Private::moveCursorCb(VTermPos pos, VTermPos, int visible, void *user)
{
    Private *d = static_cast<Private *>(user);
    d->cursor = pos;
    d->cursorVisible = visible;
    return 1;
}

int Terminal::Private::setTermPropCb(VTermProp prop, VTermValue *val, void *user)
{
    Private *d = static_cast<Private *>(user);
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
        d->cursorVisible = val->boolean;
        break;
    case VTERM_PROP_ALTSCREEN:
        d->altScreen = val->boolean;
        break;
    case VTERM_PROP_CURSORSHAPE:
        d->cursorShape = val->number;
        break;
    case VTERM_PROP_MOUSE:
        d->mouseMode = val->number;
        break;
    case VTERM_PROP_REVERSE:
        d->reverseVideo = val->boolean;
        break;
    case VTERM_PROP_TITLE: {
        const VTermStringFragment &f = val->string;
        if (f.initial)
            d->titleBuf.clear();
        if (f.str && f.len && d->titleBuf.size() < kMaxTitle)
            d->titleBuf.append(f.str, int(qMin<size_t>(f.len, size_t(kMaxTitle - d->titleBuf.size()))));
        if (f.final) {
            const QString t = QString::fromUtf8(d->titleBuf);
            d->titleBuf.clear();
            if (t != d->title) {
                d->title = t;
                emit d->q->titleChanged();
            }
        }
        break;
    }
    default:
        break;
    }
    d->markDirty();
    return 1;
}

int Terminal::Private::bellCb(void *user)
{
    emit static_cast<Private *>(user)->q->bell();
    return 1;
}

int Terminal::Private::resizeCb(int rows, int cols, void *user)
{
    Private *d = static_cast<Private *>(user);
    d->rows = rows;
    d->cols = cols;
    return 1;
}

int Terminal::Private::sbPushLineCb(int cols, const VTermScreenCell *cells, void *user)
{
    static_cast<Private *>(user)->pushLine(cols, cells);
    return 1;
}

int Terminal::Private::sbPopLineCb(int cols, VTermScreenCell *cells, void *user)
{
    return static_cast<Private *>(user)->popLine(cols, cells) ? 1 : 0;
}

int Terminal::Private::sbClearCb(void *user)
{
    static_cast<Private *>(user)->clearScrollback();
    return 1;
}

int Terminal::Private::selectionSetCb(VTermSelectionMask, VTermStringFragment frag, void *user)
{
    // OSC 52 write; libvterm has already decoded the base64.
    Private *d = static_cast<Private *>(user);
    if (frag.initial) {
        d->clipBuf.clear();
        d->clipValid = true;
    }
    if (frag.str && frag.len) {
        if (d->clipBuf.size() + int(frag.len) > kMaxClipboard)
            d->clipValid = false;
        else
            d->clipBuf.append(frag.str, int(frag.len));
    }
    if (frag.final) {
        // An empty write would clear the clipboard; ignore it, as well as oversized
        // or malformed ones.
        if (d->clipValid && !d->clipBuf.isEmpty())
            emit d->q->clipboardWriteRequested(QString::fromUtf8(d->clipBuf));
        d->clipBuf.clear();
        d->clipValid = false;
    }
    return 1;
}

int Terminal::Private::selectionQueryCb(VTermSelectionMask, void *)
{
    // OSC 52 read: refused, the remote side never gets our clipboard.
    return 1;
}

// ---------------------------------------------------------------------------
// Terminal

Terminal::Terminal(QObject *parent)
    : QObject(parent)
    , d(new Private(this))
{
}

Terminal::~Terminal()
{
    delete d;
}

void Terminal::feed(const QByteArray &data)
{
    if (data.isEmpty())
        return;
    if (!d->pending.isEmpty()) {
        d->pending.append(data);
        d->schedulePass();
        return;
    }
    d->pending = data;
    d->pendingOffset = 0;
    d->process();
}

void Terminal::resize(int cols, int rows)
{
    d->resize(cols, rows);
}

QString Terminal::title() const
{
    return d->title;
}

int Terminal::columns() const
{
    return d->cols;
}

int Terminal::rows() const
{
    return d->rows;
}

bool Terminal::ctrlModifier() const
{
    return d->ctrl;
}

void Terminal::setCtrlModifier(bool on)
{
    if (d->ctrl == on)
        return;
    d->ctrl = on;
    emit ctrlModifierChanged();
}

bool Terminal::altModifier() const
{
    return d->alt;
}

void Terminal::setAltModifier(bool on)
{
    if (d->alt == on)
        return;
    d->alt = on;
    emit altModifierChanged();
}

void Terminal::sendText(const QString &text)
{
    if (text.isEmpty())
        return;
    const bool c = d->ctrl;
    const bool a = d->alt;
    d->consumeModifiers();
    d->sendText(text, c, a);
}

void Terminal::sendKey(const QString &name)
{
    const QString n = name.toLower();
    int mod = VTERM_MOD_NONE;
    if (d->ctrl)
        mod |= VTERM_MOD_CTRL;
    if (d->alt)
        mod |= VTERM_MOD_ALT;
    d->consumeModifiers();

    if (n == QLatin1String("backtab")) {
        d->sendKey(VTERM_KEY_TAB, VTermModifier(mod | VTERM_MOD_SHIFT));
        return;
    }
    for (const KeyName &k : kKeyNames) {
        if (n == QLatin1String(k.name)) {
            d->sendKey(k.key, VTermModifier(mod));
            return;
        }
    }
    if (n.size() >= 2 && n.at(0) == QLatin1Char('f')) {
        bool ok = false;
        const int f = n.mid(1).toInt(&ok);
        if (ok && f >= 1 && f <= 12) {
            d->sendKey(VTermKey(VTERM_KEY_FUNCTION(f)), VTermModifier(mod));
            return;
        }
    }
    qWarning() << "Terminal::sendKey: unknown key" << name;
}

void Terminal::paste(const QString &text)
{
    if (text.isEmpty())
        return;
    QString t = text;
    t.replace(QLatin1String("\r\n"), QLatin1String("\r"));
    t.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    // Never let pasted text end bracketed paste early.
    t.remove(QLatin1String("\x1b[200~"));
    t.remove(QLatin1String("\x1b[201~"));
    vterm_keyboard_start_paste(d->vt);
    d->sendBytes(t.toUtf8());
    vterm_keyboard_end_paste(d->vt);
    d->noteInput();
}

QStringList Terminal::screenLines() const
{
    QStringList lines;
    TermLine l;
    for (int r = 0; r < d->rows; ++r) {
        d->line(r, l);
        QString s = d->lineText(l);
        int end = s.size();
        while (end > 0 && s.at(end - 1) == QLatin1Char(' '))
            --end;
        s.truncate(end);
        lines << s;
    }
    return lines;
}
