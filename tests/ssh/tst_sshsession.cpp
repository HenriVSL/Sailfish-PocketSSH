// Host tests for SshSession against throwaway sshd instances started by run.sh.
//
// Environment (set by run.sh):
//   SSHTEST_DIR            scratch dir with client keys (id_*) and host keys (host_*)
//   SSHTEST_USER           user name to log in as
//   SSHTEST_PORT_ED25519 / _ECDSA / _RSA   sshd instances, one host key type each
//   SSHTEST_FP_ED25519 / _ECDSA / _RSA     `ssh-keygen -lf` fingerprints of those keys
//   SSHTEST_CLOSED_PORT    a local port with nothing listening
// Optional, for remoteSailfish (run inside `sfdev run emu -- ...`):
//   SFDEV_HOST, SFDEV_PORT, SFDEV_USER, SSHTEST_REMOTE_KEY

#include "ssh/sshsession.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QStringList>
#include <QThread>
#include <QtTest>

#include <functional>
#include <memory>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

QString env(const char *name) { return QString::fromLocal8Bit(qgetenv(name)); }

QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("cannot read %s", qPrintable(path));
    return f.readAll();
}

QByteArray key(const QString &name) { return readFile(env("SSHTEST_DIR") + QLatin1Char('/') + name); }

bool waitUntil(const std::function<bool()> &cond, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return true;
}

// Wraps one session with spies and an output buffer.
struct Client
{
    std::unique_ptr<SshSession> s{new SshSession};
    QSignalSpy hostKey{s.get(), SIGNAL(hostKeyReceived(SshHostKey))};
    QSignalSpy methods{s.get(), SIGNAL(authMethodsReceived(QStringList, bool))};
    QSignalSpy authOk{s.get(), SIGNAL(authenticated())};
    QSignalSpy authFail{s.get(), SIGNAL(authenticationFailed(QString))};
    QSignalSpy shell{s.get(), SIGNAL(shellStarted())};
    QSignalSpy closed{s.get(), SIGNAL(closed(QString))};
    QByteArray out;

    Client()
    {
        QObject::connect(s.get(), &SshSession::dataReceived, [this](const QByteArray &d) { out += d; });
    }
    bool wait(QSignalSpy &spy, int n = 1, int ms = 10000)
    {
        return waitUntil([&] { return spy.count() >= n || (&spy != &closed && closed.count() > 0); }, ms)
               && spy.count() >= n;
    }
    QString closeError() const { return closed.isEmpty() ? QStringLiteral("<open>") : closed.first().first().toString(); }
    QString lastAuthError() const { return authFail.isEmpty() ? QString() : authFail.last().first().toString(); }
    bool waitOutput(const QByteArray &needle, int ms = 10000)
    {
        return waitUntil([&] { return out.contains(needle) || closed.count() > 0; }, ms) && out.contains(needle);
    }
};

int port(const char *name) { return env(name).toInt(); }

// A listening socket that never accepts: the kernel completes the TCP handshake,
// but no SSH banner ever arrives.
struct SilentServer
{
    int fd = -1;
    int port = 0;
    SilentServer()
    {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (::bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0 || ::listen(fd, 8) != 0)
            qFatal("SilentServer: bind/listen failed");
        socklen_t len = sizeof a;
        ::getsockname(fd, reinterpret_cast<sockaddr *>(&a), &len);
        port = ntohs(a.sin_port);
    }
    ~SilentServer() { ::close(fd); }
};

const char *kBlackhole = "10.255.255.1";

} // namespace

class TestSshSession : public QObject
{
    Q_OBJECT

private:
    // connect + key auth on the ed25519 server; returns false on failure.
    bool login(Client &c, const QString &keyName = QStringLiteral("id_ed25519"), int p = 0)
    {
        c.s->connectToHost(QStringLiteral("127.0.0.1"), p ? p : port("SSHTEST_PORT_ED25519"));
        if (!c.wait(c.hostKey))
            return false;
        c.s->authenticateWithKey(env("SSHTEST_USER"), key(keyName));
        return c.wait(c.authOk);
    }
    bool loginShell(Client &c, int cols = 80, int rows = 24)
    {
        if (!login(c))
            return false;
        c.s->startShell(cols, rows);
        if (!c.wait(c.shell))
            return false;
        // Make the output predictable: no prompt, no echo of our commands.
        c.s->write("PS1=''; stty -echo; echo READY-$((40+2))\n");
        return c.waitOutput("READY-42");
    }

private slots:
    void initTestCase()
    {
        QVERIFY2(!env("SSHTEST_DIR").isEmpty(), "run through tests/ssh/run.sh");
    }

    void hostKeyFingerprint_data()
    {
        QTest::addColumn<QString>("portVar");
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("fpVar");
        QTest::addColumn<QString>("pubFile");
        QTest::newRow("ed25519") << "SSHTEST_PORT_ED25519" << "ssh-ed25519" << "SSHTEST_FP_ED25519" << "host_ed25519.pub";
        QTest::newRow("ecdsa") << "SSHTEST_PORT_ECDSA" << "ecdsa-sha2-nistp256" << "SSHTEST_FP_ECDSA" << "host_ecdsa.pub";
        QTest::newRow("rsa") << "SSHTEST_PORT_RSA" << "ssh-rsa" << "SSHTEST_FP_RSA" << "host_rsa.pub";
    }
    void hostKeyFingerprint()
    {
        QFETCH(QString, portVar);
        QFETCH(QString, type);
        QFETCH(QString, fpVar);
        QFETCH(QString, pubFile);
        Client c;
        c.s->connectToHost(QStringLiteral("127.0.0.1"), port(portVar.toLatin1().constData()));
        QVERIFY2(c.wait(c.hostKey), qPrintable(c.closeError()));
        const SshHostKey k = c.hostKey.first().first().value<SshHostKey>();
        QCOMPARE(k.type, type);
        QCOMPARE(k.fingerprint, env(fpVar.toLatin1().constData()));
        const QList<QByteArray> pub = key(pubFile).trimmed().split(' ');
        QCOMPARE(k.keyBase64, QString::fromLatin1(pub.value(1)));
        QCOMPARE(QString::fromLatin1(pub.value(0)), type);
    }

    void authMethods()
    {
        Client c;
        c.s->connectToHost(QStringLiteral("localhost"), port("SSHTEST_PORT_ED25519"));
        QVERIFY2(c.wait(c.hostKey), qPrintable(c.closeError()));
        c.s->queryAuthMethods(env("SSHTEST_USER"));
        QVERIFY2(c.wait(c.methods), qPrintable(c.closeError()));
        QCOMPARE(c.methods.first().at(0).toStringList(), QStringList() << "publickey");
        QCOMPARE(c.methods.first().at(1).toBool(), false);
        // Password auth isn't offered by this server: refused without closing.
        c.s->authenticateWithPassword(env("SSHTEST_USER"), QStringLiteral("x"));
        QVERIFY(c.wait(c.authFail));
        QCOMPARE(c.lastAuthError(), QStringLiteral("The server does not accept password authentication"));
        QCOMPARE(c.closed.count(), 0);
    }

    void passwordRefusedThenKey()
    {
        // The RSA-host-key server offers "password" but can't verify any (non-root).
        Client c;
        c.s->connectToHost(QStringLiteral("127.0.0.1"), port("SSHTEST_PORT_RSA"));
        QVERIFY2(c.wait(c.hostKey), qPrintable(c.closeError()));
        const QString user = env("SSHTEST_USER");
        c.s->queryAuthMethods(user);
        QVERIFY(c.wait(c.methods));
        const QStringList methods = c.methods.first().at(0).toStringList();
        qInfo("methods: %s", qPrintable(methods.join(",")));
        QVERIFY(methods.contains("password"));
        for (int i = 1; i <= 3; ++i) {
            c.s->authenticateWithPassword(user, QStringLiteral("wrong password %1").arg(i));
            QVERIFY2(c.wait(c.authFail, i, 20000), qPrintable(c.closeError()));
            QCOMPARE(c.lastAuthError(), QStringLiteral("Wrong user name or password"));
        }
        QCOMPARE(c.closed.count(), 0);
        c.s->authenticateWithKey(user, key("id_rsa"));
        QVERIFY2(c.wait(c.authOk), qPrintable(c.lastAuthError() + " / " + c.closeError()));
    }

    void keyboardInteractiveRefusedThenKey()
    {
        // The ECDSA-host-key server offers keyboard-interactive but not password.
        Client c;
        c.s->connectToHost(QStringLiteral("127.0.0.1"), port("SSHTEST_PORT_ECDSA"));
        QVERIFY2(c.wait(c.hostKey), qPrintable(c.closeError()));
        const QString user = env("SSHTEST_USER");
        c.s->queryAuthMethods(user);
        QVERIFY(c.wait(c.methods));
        const QStringList methods = c.methods.first().at(0).toStringList();
        qInfo("methods: %s", qPrintable(methods.join(",")));
        QVERIFY(methods.contains("keyboard-interactive"));
        QVERIFY(!methods.contains("password"));
        for (int i = 1; i <= 3; ++i) {
            c.s->authenticateWithPassword(user, QStringLiteral("wrong"));
            QVERIFY2(c.wait(c.authFail, i, 20000), qPrintable(c.closeError()));
            QCOMPARE(c.lastAuthError(), QStringLiteral("Wrong user name or password"));
        }
        c.s->authenticateWithKey(user, key("id_ed25519"));
        QVERIFY2(c.wait(c.authOk), qPrintable(c.lastAuthError() + " / " + c.closeError()));
    }

    void publicKeyAuth_data()
    {
        QTest::addColumn<QString>("keyFile");
        QTest::newRow("ed25519") << "id_ed25519";
        QTest::newRow("rsa-openssh") << "id_rsa";
        QTest::newRow("rsa-pem") << "id_rsa_pem";
        QTest::newRow("rsa-pkcs8") << "id_rsa_pkcs8";
        QTest::newRow("ecdsa-256") << "id_ecdsa";
        QTest::newRow("ecdsa-384-pem") << "id_ecdsa384_pem";
        QTest::newRow("ecdsa-521") << "id_ecdsa521";
    }
    void publicKeyAuth()
    {
        QFETCH(QString, keyFile);
        Client c;
        c.s->connectToHost(QStringLiteral("127.0.0.1"), port("SSHTEST_PORT_ED25519"));
        QVERIFY2(c.wait(c.hostKey), qPrintable(c.closeError()));
        c.s->queryAuthMethods(env("SSHTEST_USER"));
        QVERIFY(c.wait(c.methods));
        c.s->authenticateWithKey(env("SSHTEST_USER"), key(keyFile));
        QVERIFY2(c.wait(c.authOk), qPrintable(c.lastAuthError() + " / " + c.closeError()));
        QCOMPARE(c.authFail.count(), 0);
    }

    void encryptedKey_data()
    {
        QTest::addColumn<QString>("keyFile");
        QTest::newRow("ed25519-openssh") << "id_ed25519_enc";
        QTest::newRow("rsa-pem") << "id_rsa_pem_enc";
        QTest::newRow("ecdsa-pkcs8") << "id_ecdsa_pkcs8_enc";
    }
    void encryptedKey()
    {
        QFETCH(QString, keyFile);
        Client c;
        c.s->connectToHost(QStringLiteral("127.0.0.1"), port("SSHTEST_PORT_ED25519"));
        QVERIFY2(c.wait(c.hostKey), qPrintable(c.closeError()));
        const QString user = env("SSHTEST_USER");
        c.s->authenticateWithKey(user, key(keyFile));
        QVERIFY(c.wait(c.authFail, 1));
        QCOMPARE(c.lastAuthError(), QStringLiteral("The private key is encrypted; enter its passphrase"));
        c.s->authenticateWithKey(user, key(keyFile), QStringLiteral("wrong passphrase"));
        QVERIFY(c.wait(c.authFail, 2));
        QCOMPARE(c.lastAuthError(), QStringLiteral("Wrong passphrase for the private key"));
        c.s->authenticateWithKey(user, key(keyFile), QStringLiteral("correct horse"));
        QVERIFY2(c.wait(c.authOk), qPrintable(c.lastAuthError() + " / " + c.closeError()));
        QCOMPARE(c.closed.count(), 0);
    }

    void wrongKeyThenRetry()
    {
        Client c;
        c.s->connectToHost(QStringLiteral("127.0.0.1"), port("SSHTEST_PORT_ED25519"));
        QVERIFY2(c.wait(c.hostKey), qPrintable(c.closeError()));
        const QString user = env("SSHTEST_USER");
        c.s->authenticateWithKey(user, "this is not a key");
        QVERIFY(c.wait(c.authFail, 1));
        QCOMPARE(c.lastAuthError(), QStringLiteral("Not a private key (expected OpenSSH or PEM format)"));
        c.s->authenticateWithKey(user, key("id_ed25519.pub"));
        QVERIFY(c.wait(c.authFail, 2));
        QCOMPARE(c.lastAuthError(), QStringLiteral("This is a public key; the private key is needed"));
        c.s->authenticateWithKey(user, key("id_wrong_rsa"));
        QVERIFY(c.wait(c.authFail, 3));
        QCOMPARE(c.lastAuthError(), QStringLiteral("The server did not accept the key"));
        c.s->authenticateWithKey(user, key("id_wrong_ed25519"));
        QVERIFY(c.wait(c.authFail, 4));
        QCOMPARE(c.lastAuthError(), QStringLiteral("The server did not accept the key"));
        QCOMPARE(c.closed.count(), 0);
        // A different key type than the refused one, on the same connection.
        c.s->authenticateWithKey(user, key("id_ecdsa"));
        QVERIFY2(c.wait(c.authOk), qPrintable(c.lastAuthError() + " / " + c.closeError()));
        c.s->startShell(80, 24);
        QVERIFY2(c.wait(c.shell), qPrintable(c.closeError()));
    }

    void shellEcho()
    {
        Client c;
        QVERIFY2(loginShell(c), qPrintable(c.closeError() + " out: " + c.out));
        c.out.clear();
        c.s->write("echo \"$TERM\" hello-$((6*7))\n");
        QVERIFY2(c.waitOutput("xterm-256color hello-42"), c.out.constData());
        // Bytes in both directions are passed through untouched (UTF-8 here).
        c.out.clear();
        c.s->write("printf '\\303\\244\\342\\202\\254-%s\\n' ok\n");
        QVERIFY2(c.waitOutput("\xc3\xa4\xe2\x82\xac-ok"), c.out.constData());
    }

    void resize()
    {
        Client c;
        QVERIFY2(loginShell(c, 80, 24), qPrintable(c.closeError()));
        c.out.clear();
        c.s->write("stty size\n");
        QVERIFY2(c.waitOutput("24 80"), c.out.constData());
        // Coalesced: only the last size counts.
        c.s->resize(90, 20);
        c.s->resize(100, 30);
        c.s->resize(132, 43);
        QThread::msleep(200);
        c.out.clear();
        c.s->write("stty size\n");
        QVERIFY2(c.waitOutput("43 132"), c.out.constData());
    }

    void exitClosesCleanly()
    {
        Client c;
        QVERIFY2(loginShell(c), qPrintable(c.closeError()));
        c.s->write("echo bye; exit\n");
        QVERIFY(c.wait(c.closed));
        QCOMPARE(c.closeError(), QString());
        QVERIFY(c.out.contains("bye"));
        QVERIFY(c.s->isClosed());
        // Inert afterwards; still exactly one closed().
        c.s->write("echo more\n");
        c.s->disconnectFromHost();
        QTest::qWait(300);
        QCOMPARE(c.closed.count(), 1);
    }

    void serverDiesMidShell()
    {
        Client c;
        QVERIFY2(loginShell(c), qPrintable(c.closeError()));
        // The shell's parent is the sshd session process: kill it, no SSH goodbye.
        c.s->write("kill -9 $PPID\n");
        QVERIFY(c.wait(c.closed));
        QCOMPARE(c.closeError(), QStringLiteral("Connection lost"));
    }

    void largeOutput()
    {
        Client c;
        QVERIFY2(loginShell(c), qPrintable(c.closeError()));
        c.out.clear();
        QElapsedTimer t;
        t.start();
        c.s->write("seq 1 200000; echo END-$((1+1))\n");
        QVERIFY2(c.waitOutput("END-2", 60000), qPrintable(c.closeError()));
        const qint64 ms = t.elapsed();
        QByteArray text = c.out;
        text.replace('\r', "");
        const QList<QByteArray> lines = text.split('\n');
        // Every all-digit line must be the next number; anything else (the shell's
        // bracketed-paste escapes) may only appear outside the run of numbers.
        int expect = 1;
        int other = 0;
        for (const QByteArray &line : lines) {
            if (line.startsWith("END-"))
                break;
            bool digits = !line.isEmpty();
            for (char ch : line)
                digits = digits && ch >= '0' && ch <= '9';
            if (!digits) {
                if (!line.isEmpty())
                    ++other;
                continue;
            }
            QCOMPARE(line, QByteArray::number(expect));
            ++expect;
        }
        QCOMPARE(expect - 1, 200000);
        QVERIFY2(other <= 2, qPrintable(QString::number(other)));
        qInfo("seq 1 200000: %d bytes in %lld ms", c.out.size(), ms);
    }

    void largeInput()
    {
        Client c;
        QVERIFY2(loginShell(c), qPrintable(c.closeError()));
        // 1 MB through stdin in one write: exercises partial writes and the window.
        c.s->write("stty raw -echo; head -c 1048576 | wc -c; stty sane\n");
        QThread::msleep(300);
        QByteArray blob(1048576, 'x');
        for (int i = 0; i < blob.size(); i += 61)
            blob[i] = 'y';
        c.out.clear();
        c.s->write(blob);
        QVERIFY2(c.waitOutput("1048576", 30000), c.out.constData());
    }

    void connectionRefused()
    {
        Client c;
        c.s->connectToHost(QStringLiteral("127.0.0.1"), port("SSHTEST_CLOSED_PORT"));
        QVERIFY(c.wait(c.closed));
        QCOMPARE(c.closeError(), QStringLiteral("Connection refused"));
        QCOMPARE(c.hostKey.count(), 0);
    }

    void hostNotFound()
    {
        Client c;
        c.s->connectToHost(QStringLiteral("no-such-host.invalid"), 22);
        QVERIFY(c.wait(c.closed, 1, 30000));
        QCOMPARE(c.closeError(), QStringLiteral("Host not found"));
    }

    void connectTimeout()
    {
        Client c;
        QElapsedTimer t;
        t.start();
        c.s->connectToHost(QString::fromLatin1(kBlackhole), 22, 1500);
        QVERIFY(c.wait(c.closed, 1, 10000));
        const qint64 ms = t.elapsed();
        QCOMPARE(c.closeError(), QStringLiteral("Connection timed out"));
        QVERIFY2(ms >= 1400 && ms < 3500, qPrintable(QString::number(ms)));
    }

    void handshakeTimeout()
    {
        SilentServer srv;
        Client c;
        QElapsedTimer t;
        t.start();
        c.s->connectToHost(QStringLiteral("127.0.0.1"), srv.port, 1500);
        QVERIFY(c.wait(c.closed, 1, 10000));
        QCOMPARE(c.closeError(), QStringLiteral("Connection timed out"));
        QVERIFY2(t.elapsed() < 4000, qPrintable(QString::number(t.elapsed())));
    }

    void disconnectMidConnect()
    {
        Client c;
        c.s->connectToHost(QString::fromLatin1(kBlackhole), 22, 15000);
        QTest::qWait(300);
        QElapsedTimer t;
        t.start();
        c.s->disconnectFromHost();
        QVERIFY(c.wait(c.closed, 1, 2000));
        QCOMPARE(c.closeError(), QString());
        c.s.reset();
        QVERIFY2(t.elapsed() < 1000, qPrintable(QString::number(t.elapsed())));
        QCOMPARE(c.closed.count(), 1);
    }

    void destroyMidConnect()
    {
        Client c;
        c.s->connectToHost(QString::fromLatin1(kBlackhole), 22, 15000);
        QTest::qWait(300);
        QElapsedTimer t;
        t.start();
        c.s.reset();
        QVERIFY2(t.elapsed() < 500, qPrintable(QString::number(t.elapsed())));
    }

    void disconnectMidHandshake()
    {
        SilentServer srv;
        Client c;
        c.s->connectToHost(QStringLiteral("127.0.0.1"), srv.port, 15000);
        QTest::qWait(300);
        QElapsedTimer t;
        t.start();
        c.s->disconnectFromHost();
        QVERIFY(c.wait(c.closed, 1, 2000));
        QCOMPARE(c.closeError(), QString());
        c.s.reset();
        QVERIFY2(t.elapsed() < 1000, qPrintable(QString::number(t.elapsed())));
        QCOMPARE(c.closed.count(), 1);
    }

    void destroyMidHandshake()
    {
        SilentServer srv;
        Client c;
        c.s->connectToHost(QStringLiteral("127.0.0.1"), srv.port, 15000);
        QTest::qWait(300);
        QElapsedTimer t;
        t.start();
        c.s.reset();
        QVERIFY2(t.elapsed() < 1500, qPrintable(QString::number(t.elapsed())));
    }

    void disconnectMidShell()
    {
        Client c;
        QVERIFY2(loginShell(c), qPrintable(c.closeError()));
        c.s->write("sleep 100\n");
        QTest::qWait(200);
        QElapsedTimer t;
        t.start();
        c.s->disconnectFromHost();
        c.s->disconnectFromHost(); // idempotent
        QVERIFY(c.wait(c.closed, 1, 2000));
        QCOMPARE(c.closeError(), QString());
        c.s.reset();
        QVERIFY2(t.elapsed() < 1500, qPrintable(QString::number(t.elapsed())));
        QCOMPARE(c.closed.count(), 1);
    }

    void destroyMidShellWithOutput()
    {
        Client c;
        QVERIFY2(loginShell(c), qPrintable(c.closeError()));
        c.s->write("yes\n");
        QVERIFY(waitUntil([&] { return c.out.size() > 100000; }, 10000));
        QElapsedTimer t;
        t.start();
        c.s.reset();
        QVERIFY2(t.elapsed() < 1500, qPrintable(QString::number(t.elapsed())));
    }

    void destroyIdleAfterHostKey()
    {
        Client c;
        c.s->connectToHost(QStringLiteral("127.0.0.1"), port("SSHTEST_PORT_ED25519"));
        QVERIFY(c.wait(c.hostKey));
        QElapsedTimer t;
        t.start();
        c.s.reset();
        QVERIFY2(t.elapsed() < 1500, qPrintable(QString::number(t.elapsed())));
    }

    void manySessionsInParallel()
    {
        std::vector<std::unique_ptr<Client>> clients;
        for (int i = 0; i < 6; ++i)
            clients.emplace_back(new Client);
        for (auto &c : clients)
            c->s->connectToHost(QStringLiteral("127.0.0.1"), port("SSHTEST_PORT_ED25519"));
        for (auto &c : clients) {
            QVERIFY2(c->wait(c->hostKey), qPrintable(c->closeError()));
            c->s->authenticateWithKey(env("SSHTEST_USER"), key("id_ed25519"));
        }
        for (auto &c : clients) {
            QVERIFY2(c->wait(c->authOk), qPrintable(c->closeError()));
            c->s->startShell(80, 24);
        }
        for (size_t i = 0; i < clients.size(); ++i) {
            QVERIFY(clients[i]->wait(clients[i]->shell));
            clients[i]->s->write(QByteArray("echo session-") + QByteArray::number(int(i) * 1000 + 7) + "\n");
        }
        for (size_t i = 0; i < clients.size(); ++i)
            QVERIFY(clients[i]->waitOutput(QByteArray("session-") + QByteArray::number(int(i) * 1000 + 7)));
    }

    // Against a real Sailfish OS sshd; only inside `sfdev run emu -- ...`.
    void remoteSailfish()
    {
        if (env("SFDEV_HOST").isEmpty() || env("SSHTEST_REMOTE_KEY").isEmpty())
            QSKIP("not running under sfdev run");
        Client c;
        c.s->connectToHost(env("SFDEV_HOST"), env("SFDEV_PORT").toInt());
        QVERIFY2(c.wait(c.hostKey), qPrintable(c.closeError()));
        const SshHostKey k = c.hostKey.first().first().value<SshHostKey>();
        qInfo("remote host key: %s %s", qPrintable(k.type), qPrintable(k.fingerprint));
        QVERIFY(k.fingerprint.startsWith("SHA256:"));
        c.s->queryAuthMethods(env("SFDEV_USER"));
        QVERIFY(c.wait(c.methods));
        qInfo("remote auth methods: %s", qPrintable(c.methods.first().at(0).toStringList().join(",")));
        c.s->authenticateWithKey(env("SFDEV_USER"), readFile(env("SSHTEST_REMOTE_KEY")));
        QVERIFY2(c.wait(c.authOk), qPrintable(c.lastAuthError() + " / " + c.closeError()));
        c.s->startShell(80, 24);
        QVERIFY2(c.wait(c.shell), qPrintable(c.closeError()));
        c.s->write("echo remote-$((6*7)); uname -m; stty size\n");
        QVERIFY2(c.waitOutput("24 80"), c.out.constData());
        QVERIFY(c.out.contains("remote-42"));
        c.s->resize(100, 30);
        c.s->write("stty size\n");
        QVERIFY2(c.waitOutput("30 100"), c.out.constData());
        c.s->write("exit\n");
        QVERIFY(c.wait(c.closed));
        QCOMPARE(c.closeError(), QString());
    }
};

QTEST_GUILESS_MAIN(TestSshSession)
#include "tst_sshsession.moc"
