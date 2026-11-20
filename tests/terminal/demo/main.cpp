// Interactive host demo: a TerminalItem connected to a local shell through a pty.
//
//   ./terminal_demo                         # desktop window running $SHELL (or bash)
//   ./terminal_demo --font-dir DIR          # load *.ttf from DIR first (e.g. DejaVu)
//   ./terminal_demo --type "htop\n" --shot out.png --delay 2500
//        types the text into the shell, waits, saves a screenshot and quits
//        (works with QT_QPA_PLATFORM=offscreen too).
//
// Mouse wheel scrolls, Ctrl+wheel zooms, long press selects, Ctrl+Shift+C/V copy/paste.
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSocketNotifier>
#include <QTimer>

#include <cerrno>
#include <csignal>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "terminal/terminal.h"
#include "terminal/terminalitem.h"

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QString typeText, shotFile, fontDir;
    int delay = 2000;
    QSize size(720, 1280);
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--type") && i + 1 < args.size())
            typeText = QString(args.at(++i)).replace(QLatin1String("\\n"), QLatin1String("\n"));
        else if (args.at(i) == QLatin1String("--shot") && i + 1 < args.size())
            shotFile = args.at(++i);
        else if (args.at(i) == QLatin1String("--delay") && i + 1 < args.size())
            delay = args.at(++i).toInt();
        else if (args.at(i) == QLatin1String("--font-dir") && i + 1 < args.size())
            fontDir = args.at(++i);
        else if (args.at(i) == QLatin1String("--size") && i + 1 < args.size()) {
            const QStringList wh = args.at(++i).split(QLatin1Char('x'));
            if (wh.size() == 2)
                size = QSize(wh.at(0).toInt(), wh.at(1).toInt());
        }
    }
    if (!fontDir.isEmpty()) {
        QDir dir(fontDir);
        for (const QString &f : dir.entryList(QStringList() << QStringLiteral("*.ttf"), QDir::Files))
            QFontDatabase::addApplicationFont(dir.filePath(f));
    }

    Terminal term;
    QQuickWindow window;
    window.setTitle(QStringLiteral("PocketSSH terminal demo"));
    window.resize(size);
    TerminalItem *item = new TerminalItem(window.contentItem());
    item->setSize(QSizeF(size));
    item->setTerminal(&term);
    QObject::connect(&window, &QQuickWindow::widthChanged, item, [&]() { item->setWidth(window.width()); });
    QObject::connect(&window, &QQuickWindow::heightChanged, item, [&]() { item->setHeight(window.height()); });
    QObject::connect(item, &TerminalItem::clicked, item, [item]() { item->forceActiveFocus(); });
    QObject::connect(item, &TerminalItem::linkActivated, [](const QString &url) { qInfo("link: %s", qPrintable(url)); });
    QObject::connect(&term, &Terminal::titleChanged, &window, [&]() { window.setTitle(term.title()); });
    QObject::connect(&term, &Terminal::bell, []() { qInfo("bell"); });
    QObject::connect(&term, &Terminal::clipboardWriteRequested, [](const QString &t) { qInfo("OSC 52: %s", qPrintable(t)); });
    window.show();
    item->forceActiveFocus();
    // Let the item size the terminal before the shell starts.
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();

    struct winsize ws = {};
    ws.ws_col = ushort(term.columns());
    ws.ws_row = ushort(term.rows());
    int master = -1;
    const pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0) {
        perror("forkpty");
        return 1;
    }
    if (pid == 0) {
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        const char *shell = getenv("SHELL");
        if (!shell || !*shell)
            shell = "/bin/bash";
        execl(shell, shell, "-i", static_cast<char *>(nullptr));
        _exit(127);
    }

    QSocketNotifier notifier(master, QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, [&]() {
        char buf[65536];
        const ssize_t n = read(master, buf, sizeof(buf));
        if (n > 0) {
            term.feed(QByteArray(buf, int(n)));
        } else if (n == 0 || errno != EAGAIN) {
            notifier.setEnabled(false);
            app.quit();
        }
    });
    QObject::connect(&term, &Terminal::output, [&](const QByteArray &data) {
        const ssize_t w = write(master, data.constData(), size_t(data.size()));
        Q_UNUSED(w)
    });
    QObject::connect(&term, &Terminal::sizeChanged, [&]() {
        struct winsize s = {};
        s.ws_col = ushort(term.columns());
        s.ws_row = ushort(term.rows());
        ioctl(master, TIOCSWINSZ, &s);
    });

    if (!typeText.isEmpty())
        QTimer::singleShot(600, [&]() { term.sendText(typeText); });
    if (!shotFile.isEmpty()) {
        QTimer::singleShot(600 + delay, [&]() {
            window.grabWindow().save(shotFile);
            qInfo("saved %s (%d x %d terminal)", qPrintable(shotFile), term.columns(), term.rows());
            app.quit();
        });
    }

    const int rc = app.exec();
    kill(pid, SIGHUP);
    waitpid(pid, nullptr, 0);
    close(master);
    return rc;
}
