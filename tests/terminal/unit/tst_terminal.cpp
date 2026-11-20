// Host unit tests for Terminal (libvterm wrapper).
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>

#include "terminal/terminal.h"
#include "terminal/terminal_p.h"

class TestTerminal : public QObject
{
    Q_OBJECT

private:
    static TermCell cellAt(Terminal &t, int row, int col)
    {
        TermLine l;
        t.d->line(row, l);
        return l.cells.value(col);
    }
    static QString scrollbackLine(Terminal &t, int fromNewest)
    {
        TermLine l;
        t.d->line(-1 - fromNewest, l);
        QString s = t.d->lineText(l);
        while (s.endsWith(QLatin1Char(' ')))
            s.chop(1);
        return s;
    }
    static int scrollbackCount(Terminal &t) { return t.d->scrollbackCount(); }
    static QByteArray collect(Terminal &t, const std::function<void()> &fn)
    {
        QByteArray out;
        QMetaObject::Connection c = QObject::connect(&t, &Terminal::output, [&out](const QByteArray &b) { out += b; });
        fn();
        QObject::disconnect(c);
        return out;
    }
    static void settle(Terminal &t)
    {
        // Let queued input drain and the coalesced contentChanged fire.
        for (int i = 0; i < 10000 && (!t.d->pending.isEmpty() || t.d->passTimer->isActive()); ++i)
            QCoreApplication::processEvents();
    }

private slots:
    void printable()
    {
        Terminal t;
        t.feed("hello\r\nworld");
        const QStringList l = t.screenLines();
        QCOMPARE(l.size(), 24);
        QCOMPARE(l.at(0), QStringLiteral("hello"));
        QCOMPARE(l.at(1), QStringLiteral("world"));
        QCOMPARE(t.columns(), 80);
        QCOMPARE(t.rows(), 24);
        t.feed(QStringLiteral("\r\nä€ 中文 😀 é").toUtf8());
        QCOMPARE(t.screenLines().at(2), QStringLiteral("ä€ 中文 😀 é"));
        // Wide characters take two cells; the right half is a WideTail.
        const TermCell zh = cellAt(t, 2, 3);
        QCOMPARE(zh.ch, 0x4E2Du);
        QCOMPARE(int(zh.width), 2);
        QCOMPARE(cellAt(t, 2, 4).ch, TermCell::WideTail);
        QCOMPARE(cellAt(t, 2, 5).ch, 0x6587u);
        QCOMPARE(cellAt(t, 2, 8).ch, 0x1F600u);
        QCOMPARE(int(cellAt(t, 2, 8).width), 2);
        // Combining accent stays in the cell of its base character.
        QCOMPARE(cellAt(t, 2, 11).ch, uint('e'));
        QCOMPARE(int(cellAt(t, 2, 11).ncomb), 1);
    }

    void colorsAndAttributes()
    {
        Terminal t;
        t.feed("\x1b[1;3;4;7;9mA\x1b[0mB\x1b[38;5;123mC\x1b[38;2;10;20;30;48;5;17mD\x1b[0m"
               "\x1b[2mE\x1b[22mF\x1b[38;5;2mG\x1b[0m\x1b[31mH\x1b[1mI\x1b[0m\x1b[8mJ\x1b[0m"
               "\x1b[4:3mK\x1b[0m\x1b[44mL\x1b[0m");
        const TermCell a = cellAt(t, 0, 0);
        QCOMPARE(a.ch, uint('A'));
        QVERIFY(a.attrs & TermCell::Bold);
        QVERIFY(a.attrs & TermCell::Italic);
        QCOMPARE(a.underline(), 1);
        QVERIFY(a.attrs & TermCell::Reverse);
        QVERIFY(a.attrs & TermCell::Strike);
        QVERIFY(!(a.attrs & TermCell::Faint));

        const TermCell b = cellAt(t, 0, 1);
        QCOMPARE(int(b.attrs), 0);
        QVERIFY(VTERM_COLOR_IS_DEFAULT_FG(&b.fg));
        QVERIFY(VTERM_COLOR_IS_DEFAULT_BG(&b.bg));

        const TermCell c = cellAt(t, 0, 2);
        QVERIFY(VTERM_COLOR_IS_INDEXED(&c.fg));
        QCOMPARE(int(c.fg.indexed.idx), 123);

        const TermCell d = cellAt(t, 0, 3);
        QVERIFY(VTERM_COLOR_IS_RGB(&d.fg));
        QCOMPARE(int(d.fg.rgb.red), 10);
        QCOMPARE(int(d.fg.rgb.green), 20);
        QCOMPARE(int(d.fg.rgb.blue), 30);
        QVERIFY(VTERM_COLOR_IS_INDEXED(&d.bg));
        QCOMPARE(int(d.bg.indexed.idx), 17);

        // SGR 2 (faint) through the parser hook; 22 ends it.
        QVERIFY(cellAt(t, 0, 4).attrs & TermCell::Faint);
        QVERIFY(!(cellAt(t, 0, 5).attrs & TermCell::Faint));
        // 38;5;2 is color index 2, not "faint".
        const TermCell g = cellAt(t, 0, 6);
        QVERIFY(!(g.attrs & TermCell::Faint));
        QCOMPARE(int(g.fg.indexed.idx), 2);
        // Bold brightens the 8 basic colors (xterm boldColors).
        QCOMPARE(int(cellAt(t, 0, 7).fg.indexed.idx), 1);
        QCOMPARE(int(cellAt(t, 0, 8).fg.indexed.idx), 9);
        QVERIFY(cellAt(t, 0, 9).attrs & TermCell::Conceal);
        QCOMPARE(cellAt(t, 0, 10).underline(), 3); // curly
        QCOMPARE(int(cellAt(t, 0, 11).bg.indexed.idx), 4);
    }

    void cursorMovement()
    {
        Terminal t;
        t.feed("\x1b[5;10HX");
        QCOMPARE(t.screenLines().at(4), QStringLiteral("         X"));
        QCOMPARE(t.d->cursor.row, 4);
        QCOMPARE(t.d->cursor.col, 10);
        t.feed("\x1b[2A\x1b[3DY");
        QCOMPARE(t.screenLines().at(2), QStringLiteral("       Y"));
        t.feed("\x1b[H\x1b[2J");
        QCOMPARE(t.screenLines().at(4), QString());
        QCOMPARE(t.d->cursor.row, 0);
        t.feed("\x1b[?25l");
        QVERIFY(!t.d->cursorVisible);
        t.feed("\x1b[?25h\x1b[6 q");
        QVERIFY(t.d->cursorVisible);
        QCOMPARE(t.d->cursorShape, int(VTERM_PROP_CURSORSHAPE_BAR_LEFT));
    }

    void title()
    {
        Terminal t;
        QSignalSpy spy(&t, &Terminal::titleChanged);
        t.feed("\x1b]2;hello\x07");
        QCOMPARE(t.title(), QStringLiteral("hello"));
        QCOMPARE(spy.count(), 1);
        // Split over several feeds (fragmented OSC string).
        t.feed("\x1b]0;user@ho");
        t.feed("st: ~/sr");
        t.feed("c\x1b\\");
        QCOMPARE(t.title(), QStringLiteral("user@host: ~/src"));
        QCOMPARE(spy.count(), 2);
        t.feed(QStringLiteral("\x1b]2;päivää\x07").toUtf8());
        QCOMPARE(t.title(), QStringLiteral("päivää"));
    }

    void osc52()
    {
        Terminal t;
        QSignalSpy spy(&t, &Terminal::clipboardWriteRequested);
        const QByteArray payload = QStringLiteral("héllo wörld\nline 2").toUtf8().toBase64();
        t.feed("\x1b]52;c;" + payload + "\x07");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("héllo wörld\nline 2"));

        // Fragmented over feeds, with ST terminator.
        const QByteArray big = QByteArray(10000, 'x').toBase64();
        t.feed("\x1b]52;c;" + big.left(3001));
        t.feed(big.mid(3001) + "\x1b\\");
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toString(), QString(10000, QLatin1Char('x')));

        // Reads are refused: no reply, no signal.
        const QByteArray reply = collect(t, [&]() { t.feed("\x1b]52;c;?\x07"); });
        QCOMPARE(reply, QByteArray());
        QCOMPARE(spy.count(), 2);
        // Empty write (clear) and invalid base64 are ignored.
        t.feed("\x1b]52;c;\x07");
        t.feed("\x1b]52;c;!!!\x07");
        QCOMPARE(spy.count(), 2);
        // The terminal keeps working afterwards.
        t.feed("\x1b[Hok");
        QCOMPARE(t.screenLines().at(0), QStringLiteral("ok"));
    }

    void altScreen()
    {
        Terminal t;
        t.resize(40, 10);
        for (int i = 0; i < 15; ++i)
            t.feed(QStringLiteral("main %1\r\n").arg(i).toUtf8());
        const QStringList mainLines = t.screenLines();
        const int sb = scrollbackCount(t);
        QCOMPARE(sb, 6);

        t.feed("\x1b[?1049h\x1b[H\x1b[2J");
        QVERIFY(t.d->altScreen);
        for (int i = 0; i < 50; ++i)
            t.feed(QStringLiteral("alt %1\r\n").arg(i).toUtf8());
        QCOMPARE(scrollbackCount(t), sb); // full-screen apps don't fill scrollback
        QVERIFY(t.screenLines().contains(QStringLiteral("alt 49")));
        t.feed("\x1b[?1049l");
        QVERIFY(!t.d->altScreen);
        QCOMPARE(t.screenLines(), mainLines);
        QCOMPARE(scrollbackCount(t), sb);

        t.feed("\x1b[?1049h");
        for (int i = 0; i < 50; ++i)
            t.feed(QStringLiteral("alt %1\r\n").arg(i).toUtf8());
        // Resizing while on the alternate screen must not leak alt rows into scrollback.
        t.resize(30, 6);
        t.resize(40, 12);
        t.resize(40, 10);
        for (int i = 0; i < scrollbackCount(t); ++i)
            QVERIFY(!scrollbackLine(t, i).startsWith(QLatin1String("alt")));

        QVERIFY(scrollbackCount(t) <= sb);
        t.feed("\x1b[?1049l");
        QVERIFY(!t.d->altScreen);
        QCOMPARE(t.screenLines().at(0), QStringLiteral("main 9"));
    }

    void scrollback()
    {
        Terminal t;
        for (int i = 0; i < 100; ++i)
            t.feed(QStringLiteral("line %1\r\n").arg(i).toUtf8());
        // 100 lines + empty cursor line on 24 rows -> 77 lines scrolled off.
        QCOMPARE(scrollbackCount(t), 77);
        QCOMPARE(scrollbackLine(t, 0), QStringLiteral("line 76"));
        QCOMPARE(scrollbackLine(t, 76), QStringLiteral("line 0"));
        QCOMPARE(t.screenLines().at(0), QStringLiteral("line 77"));

        // Fewer rows: the top rows go to scrollback.
        t.resize(80, 10);
        QCOMPARE(t.rows(), 10);
        QCOMPARE(t.screenLines().at(9), QString());
        QCOMPARE(t.screenLines().at(8), QStringLiteral("line 99"));
        QCOMPARE(scrollbackCount(t), 91);
        // More rows: lines come back from scrollback.
        t.resize(80, 30);
        QCOMPARE(t.screenLines().at(28), QStringLiteral("line 99"));
        QCOMPARE(t.screenLines().at(0), QStringLiteral("line 71"));
        QCOMPARE(scrollbackCount(t), 71);

        // CSI 3 J clears scrollback.
        t.feed("\x1b[3J");
        QCOMPARE(scrollbackCount(t), 0);

        // The ring is capped.
        Terminal big;
        QByteArray data;
        for (int i = 0; i < 6000; ++i)
            data += "row " + QByteArray::number(i) + "\r\n";
        big.feed(data);
        settle(big);
        QCOMPARE(scrollbackCount(big), 5000);
        QCOMPARE(scrollbackLine(big, 0), QStringLiteral("row 5976"));
        QCOMPARE(scrollbackLine(big, 4999), QStringLiteral("row 977"));
    }

    void reflow()
    {
        Terminal t;
        t.resize(20, 5);
        t.feed("abcdefghijklmnopqrstuvwxyz0123\r\n$ ");
        QCOMPARE(t.screenLines().at(0), QStringLiteral("abcdefghijklmnopqrst"));
        QCOMPARE(t.screenLines().at(1), QStringLiteral("uvwxyz0123"));
        t.resize(40, 5);
        QCOMPARE(t.screenLines().at(0), QStringLiteral("abcdefghijklmnopqrstuvwxyz0123"));
        QCOMPARE(t.screenLines().at(1), QStringLiteral("$"));
        t.resize(10, 5);
        QCOMPARE(t.screenLines().at(0), QStringLiteral("abcdefghij"));
        QCOMPARE(t.screenLines().at(2), QStringLiteral("uvwxyz0123"));
    }

    void resizeStress()
    {
        // libvterm 0.3.3 abort()s on some reflow resizes (cursor on a wrapped line that
        // no longer fits); Terminal must avoid them. Long wrapped line with the cursor
        // at its end, then shrink a lot.
        {
            Terminal t;
            t.resize(80, 12);
            t.feed(QByteArray(900, 'x'));
            t.resize(30, 6);
            t.resize(10, 4);
            t.resize(100, 20);
            QVERIFY(t.columns() == 100);
        }
        // Random content and random sizes, primary and alternate screen.
        quint32 seed = 1234;
        auto qrand = [&seed]() { seed = seed * 1103515245u + 12345u; return int((seed >> 16) & 0x7fff); };
        Terminal t;
        const char *pieces[] = {"hello world ", "\r\n", "\x1b[31mred\x1b[0m ", "中文字符", "\t", "\x1b[3A",
                                "\x1b[10C", "\x1b[?1049h", "\x1b[?1049l", "\x1b[2J", "\x1b[H", "😀",
                                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
        for (int i = 0; i < 3000; ++i) {
            const int n = qrand() % 6;
            for (int k = 0; k < n; ++k)
                t.feed(pieces[qrand() % int(sizeof(pieces) / sizeof(pieces[0]))]);
            if (qrand() % 3 == 0)
                t.resize(2 + qrand() % 120, 1 + qrand() % 50);
        }
        settle(t);
        QVERIFY(t.columns() >= 2);
    }

    void keys()
    {
        Terminal t;
        auto key = [&](const char *name) { return collect(t, [&]() { t.sendKey(QLatin1String(name)); }); };
        auto text = [&](const QString &s) { return collect(t, [&]() { t.sendText(s); }); };

        QCOMPARE(key("up"), QByteArray("\x1b[A"));
        QCOMPARE(key("left"), QByteArray("\x1b[D"));
        QCOMPARE(key("home"), QByteArray("\x1b[H"));
        t.feed("\x1b[?1h"); // DECCKM: application cursor keys
        QCOMPARE(key("up"), QByteArray("\x1bOA"));
        QCOMPARE(key("right"), QByteArray("\x1bOC"));
        t.feed("\x1b[?1l");
        QCOMPARE(key("up"), QByteArray("\x1b[A"));

        QCOMPARE(key("enter"), QByteArray("\r"));
        QCOMPARE(key("backspace"), QByteArray("\x7f"));
        QCOMPARE(key("tab"), QByteArray("\t"));
        QCOMPARE(key("esc"), QByteArray("\x1b"));
        QCOMPARE(key("pgup"), QByteArray("\x1b[5~"));
        QCOMPARE(key("delete"), QByteArray("\x1b[3~"));
        QCOMPARE(key("f1"), QByteArray("\x1bOP"));
        QCOMPARE(key("f5"), QByteArray("\x1b[15~"));
        QCOMPARE(key("f12"), QByteArray("\x1b[24~"));

        QSignalSpy ctrlSpy(&t, &Terminal::ctrlModifierChanged);
        t.setCtrlModifier(true);
        QCOMPARE(text(QStringLiteral("c")), QByteArray("\x03"));
        QVERIFY(!t.ctrlModifier()); // one-shot
        QCOMPARE(ctrlSpy.count(), 2);
        QCOMPARE(text(QStringLiteral("c")), QByteArray("c"));
        t.setCtrlModifier(true);
        QCOMPARE(text(QStringLiteral("[")), QByteArray("\x1b"));
        t.setCtrlModifier(true);
        QCOMPARE(text(QStringLiteral("D")), QByteArray("\x04"));
        t.setCtrlModifier(true);
        QCOMPARE(text(QStringLiteral(" ")), QByteArray("\0", 1));
        t.setCtrlModifier(true);
        QCOMPARE(key("up"), QByteArray("\x1b[1;5A"));
        QVERIFY(!t.ctrlModifier());
        t.setCtrlModifier(true);
        QCOMPARE(key("backspace"), QByteArray("\x08"));

        t.setAltModifier(true);
        QCOMPARE(text(QStringLiteral("x")), QByteArray("\x1bx"));
        QVERIFY(!t.altModifier());
        t.setAltModifier(true);
        QCOMPARE(key("backspace"), QByteArray("\x1b\x7f"));
        t.setAltModifier(true);
        t.setCtrlModifier(true);
        QCOMPARE(text(QStringLiteral("a")), QByteArray("\x1b\x01"));

        QCOMPARE(text(QStringLiteral("ls -l\n")), QByteArray("ls -l\r"));
        QCOMPARE(text(QStringLiteral("ä€😀")), QStringLiteral("ä€😀").toUtf8());
        QCOMPARE(key("backtab"), QByteArray("\x1b[Z"));

        QSignalSpy inputSpy(&t, &Terminal::inputSent);
        QCOMPARE(collect(t, [&]() { t.paste(QStringLiteral("echo a\necho b")); }), QByteArray("echo a\recho b"));
        t.feed("\x1b[?2004h"); // bracketed paste on
        QCOMPARE(collect(t, [&]() { t.paste(QStringLiteral("x\r\ny\x1b[201~z")); }),
                 QByteArray("\x1b[200~x\ry" "z\x1b[201~"));
        t.feed("\x1b[?2004l");
        QCOMPARE(collect(t, [&]() { t.paste(QStringLiteral("q")); }), QByteArray("q"));
        QCOMPARE(inputSpy.count(), 3);

        // Device attribute queries are answered through output().
        QVERIFY(collect(t, [&]() { t.feed("\x1b[c"); }).startsWith("\x1b[?"));
    }

    void coalescing()
    {
        Terminal t;
        QSignalSpy spy(&t, &Terminal::contentChanged);
        for (int i = 0; i < 1000; ++i)
            t.feed("x");
        QCOMPARE(spy.count(), 0); // nothing until the event loop runs
        settle(t);
        QCOMPARE(spy.count(), 1);
    }

    void throughput()
    {
        // ~8 MB of colored `ls -l`-like output and plain text, fed in 16 KB chunks like
        // an SSH channel would deliver it.
        QByteArray sample;
        int i = 0;
        while (sample.size() < 8 * 1024 * 1024) {
            sample += "-rw-r--r-- 1 user users  " + QByteArray::number(i * 37 % 100000)
                    + " Nov 20 17:25 \x1b[01;34mdirectory_" + QByteArray::number(i)
                    + "\x1b[0m  \x1b[01;32mexecutable.sh\x1b[0m plain text file name.txt\r\n";
            sample += "The quick brown fox jumps over the lazy dog; 0123456789 ABCDEFGHIJKLMNOP\r\n";
            ++i;
        }
        sample += "\r\nEND-OF-THROUGHPUT\r\n";

        Terminal t;
        t.resize(54, 40);
        QElapsedTimer total;
        total.start();
        qint64 maxFeedNs = 0;
        for (int off = 0; off < sample.size(); off += 16384) {
            QElapsedTimer f;
            f.start();
            t.feed(sample.mid(off, 16384));
            maxFeedNs = qMax(maxFeedNs, f.nsecsElapsed());
        }
        // Then drain, measuring the longest single event-loop pass (= UI stall).
        qint64 maxPassNs = 0;
        while (!t.d->pending.isEmpty() || t.d->passTimer->isActive()) {
            QElapsedTimer p;
            p.start();
            QCoreApplication::processEvents();
            maxPassNs = qMax(maxPassNs, p.nsecsElapsed());
        }
        const double secs = total.nsecsElapsed() / 1e9;
        QVERIFY(t.screenLines().contains(QStringLiteral("END-OF-THROUGHPUT")));
        const double mb = sample.size() / (1024.0 * 1024.0);
        qInfo("throughput: %.1f MB in %.3f s = %.1f MB/s; longest feed() %.1f ms, longest event-loop pass %.1f ms",
              mb, secs, mb / secs, maxFeedNs / 1e6, maxPassNs / 1e6);
        QVERIFY(maxFeedNs < 50 * 1000 * 1000);
        QVERIFY(maxPassNs < 100 * 1000 * 1000); // 12 ms per pass, 40 ms while a >4 MB backlog drains
    }
};

QTEST_GUILESS_MAIN(TestTerminal)
#include "tst_terminal.moc"
