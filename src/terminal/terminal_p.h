#ifndef TERMINAL_P_H
#define TERMINAL_P_H

// Private parts of Terminal, shared with TerminalItem (a friend of Terminal).

#include <QByteArray>
#include <QString>
#include <QVector>

#include <vterm.h>

#include "terminal/terminal.h"

class QTimer;

// One character cell, as stored in the scrollback and handed to the renderer.
struct TermCell
{
    enum Attr : quint16 {
        Bold = 1 << 0,
        UnderlineMask = 3 << 1, // VTERM_UNDERLINE_* << 1
        Italic = 1 << 3,
        Blink = 1 << 4,
        Reverse = 1 << 5,
        Conceal = 1 << 6,
        Strike = 1 << 7,
        Faint = 1 << 8,
    };
    // Unicode code point; 0 = empty, WideTail = right half of a wide character.
    quint32 ch;
    VTermColor fg;
    VTermColor bg;
    quint16 attrs;
    qint8 width;  // 1 or 2
    quint8 ncomb; // number of combining characters for this cell in TermLine::comb

    static const quint32 WideTail = 0xFFFFFFFFu;
    int underline() const { return (attrs & UnderlineMask) >> 1; }
};

struct TermLine
{
    QVector<TermCell> cells;
    QVector<quint32> comb; // combining characters, in cell order
    bool wrapped = false;  // continues on the next line (soft wrap)
};

class Terminal::Private
{
public:
    explicit Private(Terminal *q);
    ~Private();

    Terminal *q;
    VTerm *vt = nullptr;
    VTermState *state = nullptr;
    VTermScreen *screen = nullptr;
    int rows = 24;
    int cols = 80;

    QString title;
    QByteArray titleBuf;
    bool ctrl = false;
    bool alt = false;

    // Screen properties tracked from libvterm.
    VTermPos cursor = {0, 0};
    bool cursorVisible = true;
    int cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK;
    bool altScreen = false;
    int mouseMode = VTERM_PROP_MOUSE_NONE;
    bool reverseVideo = false;

    // Scrollback: a ring of at most sbCapacity lines, oldest first.
    QVector<TermLine> sb;
    int sbHead = 0;  // index of the oldest line
    int sbCount = 0;
    int sbCapacity = 5000;
    // Total lines ever pushed into scrollback (views use the difference to keep a
    // scrolled-back position steady while output arrives) and a counter bumped
    // whenever scrollback lines move for other reasons (pop, clear).
    quint64 sbPushed = 0;
    quint64 sbReset = 0;
    quint64 inputSerial = 0;

    // OSC 52
    QByteArray clipBuf;
    bool clipValid = false;
    char selectionBuffer[4096];

    // Input throttling: feed() parses for at most a time budget per event-loop pass
    // and queues the rest, so a flood of output cannot freeze the UI.
    QByteArray pending;
    int pendingOffset = 0;
    qint64 passNs = 0;
    bool dirty = false;
    QTimer *passTimer = nullptr;

    int scrollbackCount() const { return sbCount; }
    // Row in "history" coordinates: 0..rows-1 the screen, -1 the newest scrollback
    // line, -scrollbackCount() the oldest.
    void line(int row, TermLine &out) const;
    QString lineText(const TermLine &line, int from = 0, int to = -1) const;

    void process();
    void writeInput(const char *data, int len);
    void schedulePass();
    void endPass();
    void markDirty();

    void resize(int newCols, int newRows);
    bool reflowSafe(int newCols, int newRows) const;

    void sendText(const QString &text, bool ctrl, bool alt);
    void sendKey(VTermKey key, VTermModifier mod);
    void sendBytes(const QByteArray &bytes);
    void consumeModifiers();
    void noteInput();

    void pushLine(int cols, const VTermScreenCell *cells);
    bool popLine(int cols, VTermScreenCell *cells);
    void clearScrollback();

    static void cellFromVTerm(const VTermScreenCell &src, TermCell &dst, QVector<quint32> &comb);

    // libvterm callbacks
    static void outputCb(const char *s, size_t len, void *user);
    static int moveCursorCb(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int setTermPropCb(VTermProp prop, VTermValue *val, void *user);
    static int bellCb(void *user);
    static int resizeCb(int rows, int cols, void *user);
    static int sbPushLineCb(int cols, const VTermScreenCell *cells, void *user);
    static int sbPopLineCb(int cols, VTermScreenCell *cells, void *user);
    static int sbClearCb(void *user);
    static int selectionSetCb(VTermSelectionMask mask, VTermStringFragment frag, void *user);
    static int selectionQueryCb(VTermSelectionMask mask, void *user);
};

#endif // TERMINAL_P_H
