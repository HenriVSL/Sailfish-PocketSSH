#ifndef TERMINALITEM_H
#define TERMINALITEM_H

#include <QPointer>
#include <QQuickPaintedItem>

#include "terminal/terminal.h"

// Draws a Terminal and takes keyboard input for it. Registered to QML as
// PocketSSH 1.0 / TerminalItem.
//
//   TerminalItem {
//       terminal: session.terminal
//       anchors.fill: parent
//       onClicked: { forceActiveFocus(); Qt.inputMethod.show() }
//   }
//
// - Size: with resizesTerminal (default), the item's size and font decide the
//   terminal's columns/rows and calls Terminal::resize. With resizesTerminal: false
//   (the cover) it only draws, scaling the font so the terminal's current columns fit
//   the width.
// - Input: accepts input-method text (commit strings; predictive text, auto-capitals
//   and text handles are switched off through the input method hints) and key events
//   (Return, Backspace, arrows, Tab, Escape, hardware keyboards with Ctrl/Alt).
// - Touch: a vertical drag scrolls the scrollback (with flick momentum); new output
//   or typing jumps back to the bottom. A tap emits clicked(). A long press starts a
//   selection, dragging extends it. Pinch zooms (changes fontPixelSize).
// - Colors: the Pocket Shell dark palette (background #0D0F14, foreground #DADFE8,
//   cursor #4AC9A3, the 16 ANSI colors from the iOS Theme), plus 256-color and truecolor.
class TerminalItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(Terminal *terminal READ terminal WRITE setTerminal NOTIFY terminalChanged)
    Q_PROPERTY(qreal fontPixelSize READ fontPixelSize WRITE setFontPixelSize NOTIFY fontPixelSizeChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontFamilyChanged)
    Q_PROPERTY(bool resizesTerminal READ resizesTerminal WRITE setResizesTerminal NOTIFY resizesTerminalChanged)
    // Blank margin around the text in pixels, so text stays clear of rounded corners.
    Q_PROPERTY(qreal margin READ margin WRITE setMargin NOTIFY marginChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedText READ selectedText NOTIFY selectionChanged)
    // True while showing scrollback instead of the live screen.
    Q_PROPERTY(bool scrolledBack READ scrolledBack NOTIFY scrolledBackChanged)
    // Width/height of one character cell in pixels (for laying out things around it).
    Q_PROPERTY(qreal cellWidth READ cellWidth NOTIFY cellSizeChanged)
    Q_PROPERTY(qreal cellHeight READ cellHeight NOTIFY cellSizeChanged)

public:
    explicit TerminalItem(QQuickItem *parent = nullptr);
    ~TerminalItem() override;

    Terminal *terminal() const;
    void setTerminal(Terminal *terminal);
    qreal fontPixelSize() const;
    void setFontPixelSize(qreal size);
    QString fontFamily() const;
    void setFontFamily(const QString &family);
    bool resizesTerminal() const;
    void setResizesTerminal(bool on);
    qreal margin() const;
    void setMargin(qreal margin);
    bool hasSelection() const;
    QString selectedText() const;
    bool scrolledBack() const;
    qreal cellWidth() const;
    qreal cellHeight() const;

    Q_INVOKABLE void copySelection();   // to the system clipboard, then clears it
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void pasteClipboard();  // Terminal::paste with the clipboard text
    Q_INVOKABLE void scrollToBottom();

    void paint(QPainter *painter) override;

signals:
    void terminalChanged();
    void fontPixelSizeChanged();
    void fontFamilyChanged();
    void resizesTerminalChanged();
    void marginChanged();
    void selectionChanged();
    void scrolledBackChanged();
    void cellSizeChanged();
    void clicked();
    // A tap on an http(s) URL shown in the terminal (emitted instead of clicked()).
    void linkActivated(const QString &url);

    // (implementation-specific overrides and members below)
protected:
    void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void componentComplete() override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool childMouseEventFilter(QQuickItem *item, QEvent *event) override;
    void touchEvent(QTouchEvent *event) override;
    void touchUngrabEvent() override;

private:
    class Private;
    Private *d;
};

#endif // TERMINALITEM_H
