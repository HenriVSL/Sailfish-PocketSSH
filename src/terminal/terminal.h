#ifndef TERMINAL_H
#define TERMINAL_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

// The terminal emulator state for one session: an xterm-256color screen plus
// scrollback, driven by libvterm. It has no UI; TerminalItem draws it. One Terminal
// lives as long as its session tab, so scrollback survives page switches and
// reconnects, and several TerminalItems may show the same Terminal (the page and the
// cover).
//
// Input from the user (sendText/sendKey/paste) is encoded by libvterm, which follows
// the modes the remote program set (application cursor keys, bracketed paste, ...),
// and comes out of output(). Bytes from the host go into feed().
class Terminal : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(int columns READ columns NOTIFY sizeChanged)
    Q_PROPERTY(int rows READ rows NOTIFY sizeChanged)
    // One-shot modifiers for the extra-keys bar: they apply to the next sendText/sendKey
    // and then switch themselves off (emitting the NOTIFY signal).
    Q_PROPERTY(bool ctrlModifier READ ctrlModifier WRITE setCtrlModifier NOTIFY ctrlModifierChanged)
    Q_PROPERTY(bool altModifier READ altModifier WRITE setAltModifier NOTIFY altModifierChanged)

public:
    explicit Terminal(QObject *parent = nullptr);
    ~Terminal() override;

    // Bytes received from the host (UTF-8 + control sequences).
    void feed(const QByteArray &data);

    // Resize the screen. Emits sizeChanged when the size actually changes. The session
    // listens to sizeChanged and resizes the remote PTY.
    void resize(int cols, int rows);

    QString title() const;
    int columns() const;
    int rows() const;
    bool ctrlModifier() const;
    void setCtrlModifier(bool on);
    bool altModifier() const;
    void setAltModifier(bool on);

    // Typed text (from the virtual keyboard or the extra keys). With ctrlModifier,
    // a single letter or one of @[\]^_ ? space becomes the control character; with
    // altModifier the text is prefixed with ESC.
    Q_INVOKABLE void sendText(const QString &text);

    // A named special key: "enter", "backspace", "tab", "esc", "up", "down", "left",
    // "right", "home", "end", "pgup", "pgdn", "insert", "delete", "f1" ... "f12".
    // Honors the one-shot modifiers and the terminal's cursor-key mode.
    Q_INVOKABLE void sendKey(const QString &name);

    // Clipboard text, wrapped in bracketed-paste markers when the program asked for them.
    Q_INVOKABLE void paste(const QString &text);

    // The visible screen as plain text, one string per row (trailing blanks trimmed).
    Q_INVOKABLE QStringList screenLines() const;

signals:
    void output(const QByteArray &data); // bytes to send to the host
    void titleChanged();                 // OSC 0 / OSC 2
    void sizeChanged();
    void bell();
    // OSC 52 clipboard write from the remote program. Clipboard reads are refused.
    void clipboardWriteRequested(const QString &text);
    void ctrlModifierChanged();
    void altModifierChanged();
    // Screen, cursor or scrollback changed; views repaint (they may coalesce).
    void contentChanged();
    // sendText/sendKey/paste (or a TerminalItem's own key handling) just produced
    // input for the host; views jump back to the bottom of the scrollback.
    void inputSent();

private:
    class Private;
    Private *d;
    friend class TerminalItem;
    friend class TestTerminal; // host unit tests (tests/terminal)
};

#endif // TERMINAL_H
