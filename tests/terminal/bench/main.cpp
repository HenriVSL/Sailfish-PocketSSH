// Measures Terminal::feed throughput and the longest event-loop pass (UI stall) for
// ~8 MB of colored ls-like output delivered in 16 KB chunks.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QStringList>
#include <QTimer>

#include <cstdio>

#include "terminal/terminal.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const int megabytes = argc > 1 ? atoi(argv[1]) : 8;
    QByteArray sample;
    int i = 0;
    while (sample.size() < megabytes * 1024 * 1024) {
        sample += "-rw-r--r-- 1 user users  " + QByteArray::number(i * 37 % 100000)
                + " Nov 20 17:25 \x1b[01;34mdirectory_" + QByteArray::number(i)
                + "\x1b[0m  \x1b[01;32mexecutable.sh\x1b[0m plain text file name.txt\r\n";
        sample += "The quick brown fox jumps over the lazy dog; 0123456789 ABCDEFGHIJKLMNOP\r\n";
        ++i;
    }
    sample += "\r\nEND-OF-BENCH\r\n";

    Terminal term;
    term.resize(54, 40);
    int changes = 0;
    QObject::connect(&term, &Terminal::contentChanged, [&changes]() { ++changes; });

    // 1) Flood: everything arrives at once (the session reads faster than we parse).
    QElapsedTimer total;
    total.start();
    for (int off = 0; off < sample.size(); off += 16384)
        term.feed(sample.mid(off, 16384));
    qint64 maxPass = 0;
    while (!term.screenLines().contains(QStringLiteral("END-OF-BENCH"))) {
        QElapsedTimer p;
        p.start();
        QCoreApplication::processEvents();
        maxPass = qMax(maxPass, p.nsecsElapsed());
    }
    const double secs = total.nsecsElapsed() / 1e9;
    const double mb = sample.size() / (1024.0 * 1024.0);
    printf("flood: %.1f MB in %.2f s = %.1f MB/s, longest event-loop pass %.1f ms, %d repaint signals\n", mb, secs,
           mb / secs, maxPass / 1e6, changes);

    // 2) Paced: one 16 KB chunk per event-loop pass, like a socket notifier.
    Terminal term2;
    term2.resize(54, 40);
    int off = 0;
    maxPass = 0;
    total.restart();
    QTimer feeder;
    feeder.setInterval(0);
    QObject::connect(&feeder, &QTimer::timeout, [&]() {
        if (off < sample.size()) {
            term2.feed(sample.mid(off, 16384));
            off += 16384;
        }
    });
    feeder.start();
    while (!term2.screenLines().contains(QStringLiteral("END-OF-BENCH"))) {
        QElapsedTimer p;
        p.start();
        QCoreApplication::processEvents();
        maxPass = qMax(maxPass, p.nsecsElapsed());
    }
    const double secs2 = total.nsecsElapsed() / 1e9;
    printf("paced: %.1f MB in %.2f s = %.1f MB/s, longest event-loop pass %.1f ms\n", mb, secs2, mb / secs2,
           maxPass / 1e6);
    return 0;
}
