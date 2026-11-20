// Render test: draws sample terminal content with TerminalItem in an offscreen window
// and saves screenshots, and drives a few touch/keyboard interactions.
//
//   QT_QPA_PLATFORM=offscreen ./terminal_render OUTDIR [FONTDIR]
//
// Writes OUTDIR/render-main.png (phone-sized, focused), render-scrolled.png (scrolled
// back with a selection), render-cover.png (resizesTerminal: false, unfocused) and
// render-vim.png (alternate screen). Prints PASS/FAIL lines for the interaction checks.
#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include "terminal/terminal.h"
#include "terminal/terminalitem.h"

static int failures = 0;

static void check(bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

static QByteArray boxes()
{
    return QByteArray("┌────┬────┐ ╔════╦════╗ ╭────╮ ┏━━━━┓\r\n"
                      "│ ab │中文│ ║ x  ║ y  ║ │ ok │ ┃ hv ┃\r\n"
                      "├────┼────┤ ╠════╬════╣ ╰────╯ ┗━━━━┛\r\n"
                      "│😀🚀│ cd │ ║ ░▒ ║▓█▀▄║ ▁▂▃▄▅▆▇█ ╳╱╲\r\n"
                      "└────┴────┘ ╚════╩════╝ ┄┄┈┈╌╌ ▌▐▖▗▘▝\r\n"
                      "╒══╤══╕ ╓──╥──╖ ┍━━┯━━┑ ┎──┰──┒ ┠─╂─┨ ┝━┿━┥\r\n"
                      "╞══╪══╡ ╟──╫──╢ ┢━━╈━━┪ ╺━╸ ╻╹ ╼╾ \xee\x82\xb0\xee\x82\xb1\xee\x82\xb2\xee\x82\xb3\r\n"
                      "╘══╧══╛ ╙──╨──╜ ┗━━┻━━┛ ═║ ─│ ━┃\r\n");
}

static QByteArray sample()
{
    QByteArray s;
    s += "\x1b]2;render test\x07";
    s += "\x1b[1;32muser@jolla\x1b[0m:\x1b[1;34m~/src\x1b[0m$ ls --color -l\r\n";
    s += "drwxr-xr-x 2 user users 4096 Nov 20 \x1b[01;34mDocuments\x1b[0m\r\n";
    s += "-rwxr-xr-x 1 user users  812 Nov 20 \x1b[01;32mbuild.sh\x1b[0m\r\n";
    s += "lrwxrwxrwx 1 user users   11 Nov 20 \x1b[01;36mlatest\x1b[0m -> v1.2\r\n";
    s += "-rw-r--r-- 1 user users 9.1M Nov 20 \x1b[01;31mbackup.tar.gz\x1b[0m\r\n";
    s += "-rw-r--r-- 1 user users  220 Nov 20 README.md\r\n";
    s += "\x1b[1mbold\x1b[0m \x1b[3mitalic\x1b[0m \x1b[4munder\x1b[0m \x1b[4:2mdouble\x1b[0m "
         "\x1b[4:3mcurly\x1b[0m \x1b[9mstrike\x1b[0m \x1b[7mreverse\x1b[0m \x1b[2mdim\x1b[0m "
         "[\x1b[8mhidden\x1b[0m]\r\n";
    s += "\x1b[31mred \x1b[32mgreen \x1b[33myellow \x1b[34mblue \x1b[35mmagenta \x1b[36mcyan\x1b[0m\r\n";
    s += "\x1b[91mred \x1b[92mgreen \x1b[93myellow \x1b[94mblue \x1b[95mmagenta \x1b[96mcyan\x1b[0m\r\n";
    s += boxes();
    s += QString::fromUtf8("日本語 한국어 Ελληνικά Кириллица ä ö å e\xcc\x81\r\n").toUtf8();
    // 256-color strip
    for (int row = 0; row < 3; ++row) {
        for (int i = 0; i < 48 && row * 48 + i < 144; ++i)
            s += "\x1b[48;5;" + QByteArray::number(16 + row * 48 + i) + "m ";
        s += "\x1b[0m\r\n";
    }
    for (int i = 0; i < 24; ++i)
        s += "\x1b[48;5;" + QByteArray::number(232 + i) + "m  ";
    s += "\x1b[0m\r\n";
    for (int i = 0; i < 48; ++i)
        s += "\x1b[48;2;" + QByteArray::number(i * 5) + ";" + QByteArray::number(80) + ";"
                + QByteArray::number(255 - i * 5) + "m ";
    s += "\x1b[0m\r\n";
    s += "See https://sailfishos.org/develop/ for docs\r\n";
    s += "\x1b[1;32muser@jolla\x1b[0m:\x1b[1;34m~/src\x1b[0m$ echo hi";
    return s;
}

static bool waitFrames(QQuickWindow &w, int n = 3)
{
    for (int i = 0; i < n; ++i) {
        w.update();
        QTest::qWait(30);
    }
    return true;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    QDir().mkpath(outDir);
    if (argc > 2) {
        QDir fontDir(QString::fromLocal8Bit(argv[2]));
        for (const QString &f : fontDir.entryList(QStringList() << QStringLiteral("*.ttf"), QDir::Files))
            QFontDatabase::addApplicationFont(fontDir.filePath(f));
    }

    Terminal term;
    QQuickWindow window;
    window.resize(720, 1100);
    window.setColor(Qt::black);
    TerminalItem *item = new TerminalItem(window.contentItem());
    item->setSize(QSizeF(720, 1100));
    item->setTerminal(&term);
    window.show();
    QTest::qWait(50);
    printf("font %s %.0fpx, cell %.2f x %.0f, terminal %d x %d\n", qPrintable(item->fontFamily()),
           item->fontPixelSize(), item->cellWidth(), item->cellHeight(), term.columns(), term.rows());
    check(term.columns() >= 50 && term.columns() <= 60, "default font gives 50-60 columns at 720 px");

    // Some history first, so there is scrollback to scroll into.
    for (int i = 1; i <= 60; ++i)
        term.feed("history line " + QByteArray::number(i) + "\r\n");
    term.feed(sample());
    item->forceActiveFocus();
    QTest::qWait(50);
    waitFrames(window);
    check(item->hasActiveFocus(), "item has active focus");
    window.grabWindow().save(outDir + QStringLiteral("/render-main.png"));
    check(term.title() == QStringLiteral("render test"), "title from OSC 2");

    // Paint cost of a full, colorful screen (raster here; the device paints into an FBO).
    {
        QImage img(720, 1100, QImage::Format_RGB32);
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 50; ++i) {
            QPainter p(&img);
            item->paint(&p);
        }
        printf("  paint: %.2f ms per full frame (%d x %d cells)\n", timer.nsecsElapsed() / 1e6 / 50, term.columns(),
               term.rows());
    }

    // Tap on the URL -> linkActivated, not clicked.
    QSignalSpy linkSpy(item, &TerminalItem::linkActivated);
    QSignalSpy clickSpy(item, &TerminalItem::clicked);
    const QStringList lines = term.screenLines();
    int urlRow = -1;
    for (int r = 0; r < lines.size(); ++r)
        if (lines.at(r).contains(QStringLiteral("https://")))
            urlRow = r;
    const int urlCol = lines.value(urlRow).indexOf(QStringLiteral("sailfishos")) + 2;
    const QPoint urlPos(int(item->margin() + (urlCol + 0.5) * item->cellWidth()),
                        int(item->margin() + (urlRow + 0.5) * item->cellHeight()));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, urlPos);
    QTest::qWait(20);
    check(linkSpy.count() == 1 && linkSpy.at(0).at(0).toString() == QStringLiteral("https://sailfishos.org/develop/"),
          "tap on URL emits linkActivated(https://sailfishos.org/develop/)");
    check(clickSpy.count() == 0, "tap on URL does not emit clicked");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(600, 40));
    QTest::qWait(20);
    check(clickSpy.count() == 1, "tap elsewhere emits clicked");

    // Long press selects a word, dragging extends the selection.
    const QPoint wordPos(int(item->margin() + 40.5 * item->cellWidth()), int(item->margin() + (urlRow + 0.5) * item->cellHeight()));
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, wordPos);
    QTest::qWait(800);
    check(item->hasSelection(), "long press starts a selection");
    printf("  selected word: \"%s\"\n", qPrintable(item->selectedText()));
    QTest::mouseMove(&window, wordPos + QPoint(int(item->cellWidth() * 8), 0));
    QTest::qWait(20);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, wordPos + QPoint(int(item->cellWidth() * 8), 0));
    printf("  extended selection: \"%s\"\n", qPrintable(item->selectedText()));
    check(item->selectedText().startsWith(QStringLiteral("docs")), "selection text");

    // Vertical drag scrolls back into history (and then, flick).
    QSignalSpy backSpy(item, &TerminalItem::scrolledBackChanged);
    QPoint p(360, 300);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, p);
    for (int i = 0; i < 12; ++i) {
        p += QPoint(0, 30);
        QTest::mouseMove(&window, p);
        QTest::qWait(10);
    }
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, p);
    QTest::qWait(300);
    check(item->scrolledBack(), "vertical drag scrolls back");
    check(item->hasSelection(), "selection survives scrolling");
    // Output while scrolled back keeps the view in place.
    term.feed("\r\nnew output 1\r\nnew output 2\r\n");
    QTest::qWait(50);
    check(item->scrolledBack(), "output keeps scrolled-back view");
    waitFrames(window);
    window.grabWindow().save(outDir + QStringLiteral("/render-scrolled.png"));

    // Typing jumps back to the bottom and clears the selection.
    QString sent;
    QObject::connect(&term, &Terminal::output, [&sent](const QByteArray &b) { sent += QString::fromUtf8(b); });
    QTest::keyClick(&window, Qt::Key_A);
    QTest::qWait(20);
    check(!item->scrolledBack(), "typing jumps to the bottom");
    check(!item->hasSelection(), "typing clears the selection");
    QTest::keyClick(&window, Qt::Key_C, Qt::ControlModifier);
    QTest::keyClick(&window, Qt::Key_Up);
    QTest::keyClick(&window, Qt::Key_Backspace);
    QTest::keyClick(&window, Qt::Key_Return);
    QTest::keyClick(&window, Qt::Key_X, Qt::AltModifier);
    QTest::keyClick(&window, Qt::Key_F2);
    check(sent == QStringLiteral("a\x03\x1b[A\x7f\r\x1bx\x1bOQ"), "hardware keys encoded");
    printf("  sent: %s\n", qPrintable(QString::fromLatin1(sent.toUtf8().toPercentEncoding("[]~"))));

    // Input method: commit strings are sent, pre-edit is held back.
    sent.clear();
    {
        QInputMethodEvent pre(QStringLiteral("ab"), QList<QInputMethodEvent::Attribute>());
        QCoreApplication::sendEvent(item, &pre);
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("ls\n"));
        QCoreApplication::sendEvent(item, &commit);
    }
    check(sent == QStringLiteral("ls\r"), "input method commit sent, preedit held back");
    const int hints = static_cast<QQuickItem *>(item)->inputMethodQuery(Qt::ImHints).toInt();
    check((hints & Qt::ImhNoPredictiveText) && (hints & Qt::ImhNoAutoUppercase), "input method hints");
    check(item->flags() & QQuickItem::ItemAcceptsInputMethod, "ItemAcceptsInputMethod");

    // Pinch-free zoom path: font size changes resize the terminal (coalesced).
    QSignalSpy sizeSpy(&term, &Terminal::sizeChanged);
    QSignalSpy fontSpy(item, &TerminalItem::fontPixelSizeChanged);
    const int colsBefore = term.columns();
    for (int i = 0; i < 6; ++i) {
        item->setFontPixelSize(item->fontPixelSize() + 1);
        QTest::qWait(5);
    }
    QTest::qWait(300);
    check(fontSpy.count() == 6, "fontPixelSizeChanged per change");
    check(sizeSpy.count() == 1 && term.columns() < colsBefore, "font changes coalesce into one resize");
    item->setFontPixelSize(22);
    QTest::qWait(300);

    // Cover: resizesTerminal false, scaled to fit the columns, unfocused (hollow cursor).
    {
        QQuickWindow cover;
        cover.resize(234, 374);
        TerminalItem *c = new TerminalItem(cover.contentItem());
        c->setResizesTerminal(false);
        c->setMargin(4);
        c->setSize(QSizeF(234, 374));
        c->setTerminal(&term);
        cover.show();
        QTest::qWait(50);
        waitFrames(cover);
        cover.grabWindow().save(outDir + QStringLiteral("/render-cover.png"));
        check(term.columns() == colsBefore, "cover does not resize the terminal");
        printf("  cover cell %.2f x %.0f for %d columns\n", c->cellWidth(), c->cellHeight(), term.columns());
    }

    // Alternate screen, full-screen app look.
    term.feed("\x1b[?1049h\x1b[H\x1b[2J\x1b[44;97m  GNU nano 7.2            notes.txt              \x1b[K\x1b[0m\r\n");
    term.feed("\r\nThis is the alternate screen.\r\n\x1b[1mBold\x1b[0m and \x1b[4munderlined\x1b[0m.\r\n");
    term.feed("\x1b[" + QByteArray::number(term.rows() - 1) + ";1H\x1b[7m^G\x1b[0m Help  \x1b[7m^O\x1b[0m Write Out  \x1b[7m^X\x1b[0m Exit");
    term.feed("\x1b[3;5H\x1b[4 q");
    QTest::qWait(30);
    waitFrames(window);
    window.grabWindow().save(outDir + QStringLiteral("/render-vim.png"));
    term.feed("\x1b[?1049l");

    // Box drawing close-up at a large font size.
    {
        Terminal boxTerm;
        QQuickWindow w;
        w.resize(1500, 560);
        TerminalItem *b = new TerminalItem(w.contentItem());
        b->setFontPixelSize(40);
        b->setSize(QSizeF(1500, 560));
        b->setTerminal(&boxTerm);
        w.show();
        QTest::qWait(50);
        boxTerm.feed(boxes());
        waitFrames(w);
        w.grabWindow().save(outDir + QStringLiteral("/render-boxes.png"));
    }

    printf("%s (%d failures)\n", failures ? "RENDER TEST FAILED" : "RENDER TEST OK", failures);
    return failures ? 1 : 0;
}
