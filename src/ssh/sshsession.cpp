// SshSession: one SSH connection with one interactive shell, on top of libssh2.
//
// Design (ported from the iOS SSHKit SSHShellSession):
//  - Every SshSession owns one Worker, a QThread that runs its own task queue.
//    All libssh2 calls happen on that thread. Public methods append a task (a
//    std::function) to the queue and return at once.
//  - Results go back to the owner thread as functor events posted to a small
//    relay QObject that lives in the owner thread; the functor then emits the
//    public signal. (QMetaObject::invokeMethod with a functor needs Qt 5.10;
//    this works with Qt 5.6 and needs no moc for the private classes.)
//  - Connection phases (TCP connect, handshake, auth, opening the shell) run in
//    libssh2 blocking mode with a timeout. startShell then switches to
//    non-blocking mode and runs the I/O loop in the same task: read stdout and
//    stderr, flush queued writes, apply the latest pending resize, send
//    keepalives, and poll() on the socket plus an eventfd that write(),
//    resize() and disconnectFromHost() use to wake it.
//  - Teardown never hangs: disconnectFromHost() during a blocking phase shuts
//    the socket down from the caller's side, which makes the pending libssh2
//    call fail at once; the destructor does the same after a short grace period
//    and only ever waits a bounded time for the thread.

#include "sshsession.h"

#include <QCoreApplication>
#include <QEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>
#include <QtDebug>

#include <atomic>
#include <deque>
#include <functional>

#include <libssh2.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

namespace {

const int kAuthTimeoutMs = 30000;     // blocking libssh2 calls after the handshake
const int kTeardownTimeoutMs = 800;   // sending SSH_MSG_DISCONNECT etc.
const int kKeepaliveSeconds = 30;
const int kReadBudget = 256 * 1024;   // max bytes read per loop iteration
const int kChannelWindow = 2 * 1024 * 1024;
const int kChannelPacket = 32768;
const int kMaxKbdRounds = 4;          // keyboard-interactive rounds we answer

void globalInit()
{
    static const bool done = [] {
        libssh2_init(0);
        qRegisterMetaType<SshHostKey>("SshHostKey");
        return true;
    }();
    Q_UNUSED(done);
}

// ---- owner-thread relay ---------------------------------------------------

class FunctorEvent : public QEvent
{
public:
    explicit FunctorEvent(std::function<void()> fn)
        : QEvent(eventType()), fn(std::move(fn)) {}
    static QEvent::Type eventType()
    {
        static const QEvent::Type t = static_cast<QEvent::Type>(QEvent::registerEventType());
        return t;
    }
    std::function<void()> fn;
};

class Relay : public QObject
{
public:
    explicit Relay(QObject *parent) : QObject(parent) {}
    bool event(QEvent *e) override
    {
        if (e->type() == FunctorEvent::eventType()) {
            static_cast<FunctorEvent *>(e)->fn();
            return true;
        }
        return QObject::event(e);
    }
};

// ---- small helpers ----------------------------------------------------------

qint64 monotonicMs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return qint64(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

QString errnoMessage(int err)
{
    switch (err) {
    case ECONNREFUSED: return QStringLiteral("Connection refused");
    case ETIMEDOUT: return QStringLiteral("Connection timed out");
    case ENETUNREACH: return QStringLiteral("Network is unreachable");
    case EHOSTUNREACH: return QStringLiteral("No route to host");
    case EHOSTDOWN: return QStringLiteral("Host is down");
    case ECONNRESET: return QStringLiteral("Connection reset by peer");
    case EADDRNOTAVAIL: return QStringLiteral("Address not available");
    case EACCES:
    case EPERM: return QStringLiteral("Connection not permitted");
    default: return QString::fromLocal8Bit(strerror(err));
    }
}

// Reads an SSH "string" (uint32 big-endian length + bytes) at *pos.
bool readSshString(const QByteArray &buf, int *pos, QByteArray *out)
{
    if (*pos < 0 || buf.size() - *pos < 4)
        return false;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(buf.constData()) + *pos;
    const quint32 len = (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
    if (len > quint32(buf.size() - *pos - 4))
        return false;
    *out = buf.mid(*pos + 4, int(len));
    *pos += 4 + int(len);
    return true;
}

enum class KeyKind { NotAKey, PublicKey, PuttyKey, Plain, Encrypted };

// Looks at a private key's text to tell "encrypted" from "plain" (for clear error
// messages) and to reject things that aren't private keys at all.
KeyKind inspectPrivateKey(const QByteArray &key)
{
    const QByteArray k = key.trimmed();
    if (k.startsWith("PuTTY-User-Key-File"))
        return KeyKind::PuttyKey;
    if (k.startsWith("ssh-") || k.startsWith("ecdsa-") || k.startsWith("sk-")
        || k.contains("BEGIN SSH2 PUBLIC KEY") || k.contains("BEGIN PUBLIC KEY")
        || k.contains("BEGIN RSA PUBLIC KEY"))
        return KeyKind::PublicKey;
    if (!k.contains("-----BEGIN ") || !k.contains("PRIVATE KEY-----"))
        return KeyKind::NotAKey;

    if (k.contains("BEGIN ENCRYPTED PRIVATE KEY") || k.contains("Proc-Type: 4,ENCRYPTED"))
        return KeyKind::Encrypted;

    const QByteArray begin("-----BEGIN OPENSSH PRIVATE KEY-----");
    const QByteArray end("-----END OPENSSH PRIVATE KEY-----");
    const int b = k.indexOf(begin);
    if (b >= 0) {
        const int e = k.indexOf(end, b);
        if (e < 0)
            return KeyKind::NotAKey;
        const QByteArray blob = QByteArray::fromBase64(k.mid(b + begin.size(), e - b - begin.size()));
        const QByteArray magic("openssh-key-v1");
        if (!blob.startsWith(magic) || blob.size() <= magic.size() || blob.at(magic.size()) != '\0')
            return KeyKind::NotAKey;
        int pos = magic.size() + 1;
        QByteArray cipher;
        if (!readSshString(blob, &pos, &cipher))
            return KeyKind::NotAKey;
        return cipher == "none" ? KeyKind::Plain : KeyKind::Encrypted;
    }
    return KeyKind::Plain;
}

// send() without SIGPIPE on a dead socket; same contract as libssh2's _libssh2_send.
LIBSSH2_SEND_FUNC(sendNoSignal)
{
    Q_UNUSED(abstract);
    ssize_t rc;
    do {
        rc = ::send(socket, buffer, length, flags | MSG_NOSIGNAL);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0)
        return (errno == EWOULDBLOCK) ? -EAGAIN : -errno;
    return rc;
}

LIBSSH2_RECV_FUNC(recvRetry)
{
    Q_UNUSED(abstract);
    ssize_t rc;
    do {
        rc = ::recv(socket, buffer, length, flags);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0)
        return (errno == EWOULDBLOCK) ? -EAGAIN : -errno;
    return rc;
}

} // namespace

// ---- worker -----------------------------------------------------------------

class SshSession::Worker : public QThread
{
public:
    explicit Worker(SshSession *q, Relay *relay);
    ~Worker() override;

    // --- called from the owner thread ---
    void enqueue(std::function<void()> task);
    void requestStop();             // graceful when possible
    void forceShutdown();           // shut the socket down to unblock libssh2
    void queueWrite(const QByteArray &data);
    void queueResize(int cols, int rows);
    void detachRelay();
    bool stopRequested() const;
    void postClosedFromOwner(const QString &error);
    // Owner thread: may a queued result still be delivered? Not after closed(),
    // and after disconnectFromHost() nothing but closed("") is.
    static bool live(const SshSession *s) { return !s->m_closed && !(s->m_worker && s->m_worker->m_ownerStopped); }
    bool m_ownerStopped = false;    // owner thread only: disconnectFromHost() was called
    std::atomic<bool> m_resolving { false }; // inside getaddrinfo(), which can't be interrupted

    // --- worker-thread tasks ---
    void doConnect(const QString &host, int port, int timeoutMs);
    void doQueryAuth(const QString &username);
    void doAuthPassword(const QString &username, const QString &password);
    void doAuthKey(const QString &username, const QByteArray &key, const QString &passphrase);
    void doStartShell(int cols, int rows, const QString &term);

protected:
    void run() override;

private:
    enum Activity { Idle, Blocking, IoLoop };

    void setActivity(Activity a);
    void wake();
    void post(std::function<void()> fn);  // run fn in the owner thread
    void finish(const QString &error);    // emit closed(error) once and stop
    bool connectSocket(const QString &host, int port, int timeoutMs, QString *error);
    bool ensureMethods(const QString &username);
    QString lastErrorText(int *code = nullptr) const;
    QString fatalMessage(int rc, const QString &context) const;
    bool isFatalAuthError(int rc) const;
    static bool isConnectionError(int rc);
    void authFailedOrClosed(int rc, const QString &message);
    void ioLoop();
    void teardown();

    static LIBSSH2_USERAUTH_KBDINT_RESPONSE_FUNC(kbdCallback);
    static LIBSSH2_DISCONNECT_FUNC(disconnectCallback);

    SshSession *const q;

    // Shared with the owner thread, guarded by m_mutex.
    mutable QMutex m_mutex;
    QWaitCondition m_cond;
    std::deque<std::function<void()>> m_tasks;
    Relay *m_relay;
    bool m_stop = false;
    Activity m_activity = Idle;
    int m_sock = -1;
    QByteArray m_pendingWrite;
    bool m_shellActive = false;
    bool m_resizePending = false;
    int m_pendingCols = 0;
    int m_pendingRows = 0;
    const int m_wakeFd;

    // Worker thread only.
    LIBSSH2_SESSION *m_session = nullptr;
    LIBSSH2_CHANNEL *m_channel = nullptr;
    bool m_done = false;            // closed() has been sent; ignore further tasks
    bool m_methodsKnown = false;
    QStringList m_methods;
    int m_ptyCols = 0;
    int m_ptyRows = 0;
    QByteArray m_kbdPassword;
    int m_kbdRounds = 0;
    QString m_serverDisconnect;     // reason from SSH_MSG_DISCONNECT, if any
};

SshSession::Worker::Worker(SshSession *q, Relay *relay)
    : q(q)
    , m_relay(relay)
    , m_wakeFd(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))
{
    if (m_wakeFd < 0)
        qWarning("SshSession: eventfd() failed: %s", strerror(errno));
}

SshSession::Worker::~Worker()
{
    if (m_wakeFd >= 0)
        ::close(m_wakeFd);
}

void SshSession::Worker::enqueue(std::function<void()> task)
{
    QMutexLocker lock(&m_mutex);
    if (m_stop)
        return;
    m_tasks.push_back(std::move(task));
    m_cond.wakeOne();
}

void SshSession::Worker::requestStop()
{
    QMutexLocker lock(&m_mutex);
    m_stop = true;
    m_tasks.clear();
    m_cond.wakeAll();
    // A blocking libssh2 call only watches the socket: shut it down so the call
    // fails now instead of after its timeout. The I/O loop and the idle wait are
    // woken through the eventfd / condition and close gracefully.
    if (m_activity == Blocking && m_sock >= 0)
        ::shutdown(m_sock, SHUT_RDWR);
    lock.unlock();
    wake();
}

void SshSession::Worker::forceShutdown()
{
    QMutexLocker lock(&m_mutex);
    if (m_sock >= 0)
        ::shutdown(m_sock, SHUT_RDWR);
}

void SshSession::Worker::queueWrite(const QByteArray &data)
{
    {
        QMutexLocker lock(&m_mutex);
        if (m_stop || !m_shellActive)
            return;
        m_pendingWrite.append(data);
    }
    wake();
}

void SshSession::Worker::queueResize(int cols, int rows)
{
    {
        QMutexLocker lock(&m_mutex);
        if (m_stop)
            return;
        m_resizePending = true;
        m_pendingCols = cols;
        m_pendingRows = rows;
    }
    wake();
}

void SshSession::Worker::detachRelay()
{
    QMutexLocker lock(&m_mutex);
    m_relay = nullptr;
}

bool SshSession::Worker::stopRequested() const
{
    QMutexLocker lock(&m_mutex);
    return m_stop;
}

void SshSession::Worker::wake()
{
    if (m_wakeFd >= 0) {
        const quint64 one = 1;
        ssize_t r = ::write(m_wakeFd, &one, sizeof one);
        Q_UNUSED(r);
    }
}

void SshSession::Worker::setActivity(Activity a)
{
    QMutexLocker lock(&m_mutex);
    m_activity = a;
}

void SshSession::Worker::post(std::function<void()> fn)
{
    QMutexLocker lock(&m_mutex);
    if (m_relay)
        QCoreApplication::postEvent(m_relay, new FunctorEvent(std::move(fn)));
}

void SshSession::Worker::postClosedFromOwner(const QString &error)
{
    // Owner thread; m_relay is only cleared from the owner thread too.
    SshSession *session = q;
    if (m_relay) {
        QCoreApplication::postEvent(m_relay, new FunctorEvent([session, error] {
            if (session->m_closed)
                return;
            session->m_closed = true;
            emit session->closed(error);
        }));
    }
}

void SshSession::Worker::finish(const QString &error)
{
    if (m_done)
        return;
    m_done = true;
    {
        QMutexLocker lock(&m_mutex);
        m_shellActive = false;
    }
    // A user-requested stop always ends cleanly, whatever error it provoked.
    const QString err = stopRequested() ? QString() : error;
    SshSession *session = q;
    post([session, err] {
        if (session->m_closed)
            return;
        session->m_closed = true;
        emit session->closed(err);
    });
}

void SshSession::Worker::run()
{
    for (;;) {
        std::function<void()> task;
        {
            QMutexLocker lock(&m_mutex);
            while (m_tasks.empty() && !m_stop)
                m_cond.wait(&m_mutex);
            if (m_stop)
                break;
            task = std::move(m_tasks.front());
            m_tasks.pop_front();
        }
        task();
        if (m_done)
            break;
    }
    teardown();
    finish(QString()); // no-op if closed() was already sent
}

// ---- error helpers ----------------------------------------------------------

QString SshSession::Worker::lastErrorText(int *code) const
{
    if (!m_session) {
        if (code)
            *code = 0;
        return QString();
    }
    char *msg = nullptr;
    int len = 0;
    const int c = libssh2_session_last_error(m_session, &msg, &len, 0);
    if (code)
        *code = c;
    return msg ? QString::fromUtf8(msg, len) : QString();
}

QString SshSession::Worker::fatalMessage(int rc, const QString &context) const
{
    if (!m_serverDisconnect.isEmpty())
        return QStringLiteral("Disconnected by server: %1").arg(m_serverDisconnect);
    switch (rc) {
    case LIBSSH2_ERROR_TIMEOUT:
        return QStringLiteral("Connection timed out");
    case LIBSSH2_ERROR_SOCKET_DISCONNECT:
    case LIBSSH2_ERROR_SOCKET_RECV:
    case LIBSSH2_ERROR_SOCKET_SEND:
    case LIBSSH2_ERROR_SOCKET_NONE:
    case LIBSSH2_ERROR_BAD_SOCKET:
        return QStringLiteral("Connection lost");
    default:
        break;
    }
    QString detail = lastErrorText();
    if (detail.isEmpty())
        detail = QStringLiteral("error %1").arg(rc);
    return context.isEmpty() ? detail : QStringLiteral("%1: %2").arg(context, detail);
}

bool SshSession::Worker::isConnectionError(int rc)
{
    switch (rc) {
    case LIBSSH2_ERROR_TIMEOUT:
    case LIBSSH2_ERROR_SOCKET_DISCONNECT:
    case LIBSSH2_ERROR_SOCKET_RECV:
    case LIBSSH2_ERROR_SOCKET_SEND:
    case LIBSSH2_ERROR_SOCKET_NONE:
    case LIBSSH2_ERROR_BAD_SOCKET:
    case LIBSSH2_ERROR_DECRYPT:
    case LIBSSH2_ERROR_PROTO:
        return true;
    default:
        return false;
    }
}

bool SshSession::Worker::isFatalAuthError(int rc) const
{
    if (!m_serverDisconnect.isEmpty())
        return true;
    switch (rc) {
    case LIBSSH2_ERROR_AUTHENTICATION_FAILED:
    case LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED:
    case LIBSSH2_ERROR_PASSWORD_EXPIRED:
    case LIBSSH2_ERROR_FILE:
    case LIBSSH2_ERROR_METHOD_NOT_SUPPORTED:
    case LIBSSH2_ERROR_KEYFILE_AUTH_FAILED:
    case LIBSSH2_ERROR_ALGO_UNSUPPORTED:
        return false;
    default:
        return true;
    }
}

void SshSession::Worker::authFailedOrClosed(int rc, const QString &message)
{
    if (stopRequested())
        return; // run() ends with closed("")
    if (isFatalAuthError(rc)) {
        finish(fatalMessage(rc, QStringLiteral("Authentication failed")));
        return;
    }
    SshSession *session = q;
    post([session, message] {
        if (live(session))
            emit session->authenticationFailed(message);
    });
}

// ---- connect ----------------------------------------------------------------

bool SshSession::Worker::connectSocket(const QString &host, int port, int timeoutMs, QString *error)
{
    const qint64 deadline = monotonicMs() + qMax(1, timeoutMs);

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo *res = nullptr;
    const QByteArray hostName = host.trimmed().toUtf8();
    const QByteArray service = QByteArray::number(port);
    // Blocking and not interruptible; disconnectFromHost() is honoured right after.
    m_resolving = true;
    const int gai = getaddrinfo(hostName.constData(), service.constData(), &hints, &res);
    m_resolving = false;
    if (stopRequested()) {
        if (res)
            freeaddrinfo(res);
        return false;
    }
    if (gai != 0 || !res) {
        switch (gai) {
        case EAI_NONAME:
#ifdef EAI_NODATA
        case EAI_NODATA:
#endif
        case EAI_FAIL:
            *error = QStringLiteral("Host not found");
            break;
        case EAI_AGAIN:
            *error = QStringLiteral("Host not found (name lookup failed temporarily)");
            break;
        case EAI_SYSTEM:
            *error = QStringLiteral("Host not found: %1").arg(errnoMessage(errno));
            break;
        default:
            *error = QStringLiteral("Host not found: %1").arg(QString::fromLocal8Bit(gai_strerror(gai)));
            break;
        }
        if (res)
            freeaddrinfo(res);
        return false;
    }

    int count = 0;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next)
        ++count;

    QString lastError = QStringLiteral("Connection failed");
    int fd = -1;
    int index = 0;
    for (struct addrinfo *ai = res; ai && fd < 0; ai = ai->ai_next, ++index) {
        const qint64 now = monotonicMs();
        if (now >= deadline) {
            lastError = QStringLiteral("Connection timed out");
            break;
        }
        // Give each remaining address a fair share of the time left, so one
        // black-holed address (typically IPv6) doesn't use up the whole timeout.
        const qint64 remaining = deadline - now;
        const int left = count - index;
        const qint64 slice = (left <= 1) ? remaining : qMax<qint64>(qMin<qint64>(remaining, 3000), remaining / left);
        const qint64 attemptDeadline = now + slice;

        int s = ::socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, ai->ai_protocol);
        if (s < 0) {
            lastError = errnoMessage(errno);
            continue;
        }
        const int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);

        bool ok = false;
        int rc;
        do {
            rc = ::connect(s, ai->ai_addr, ai->ai_addrlen);
        } while (rc < 0 && errno == EINTR);
        if (rc == 0) {
            ok = true;
        } else if (errno == EINPROGRESS) {
            for (;;) {
                const qint64 wait = attemptDeadline - monotonicMs();
                if (wait <= 0) {
                    lastError = QStringLiteral("Connection timed out");
                    break;
                }
                struct pollfd fds[2];
                fds[0].fd = s;
                fds[0].events = POLLOUT;
                fds[0].revents = 0;
                fds[1].fd = m_wakeFd;
                fds[1].events = POLLIN;
                fds[1].revents = 0;
                const int pr = ::poll(fds, m_wakeFd >= 0 ? 2 : 1, int(qMin<qint64>(wait, 1000)));
                if (pr < 0 && errno != EINTR) {
                    lastError = errnoMessage(errno);
                    break;
                }
                if (stopRequested()) {
                    ::close(s);
                    freeaddrinfo(res);
                    return false;
                }
                if (pr > 0 && (fds[1].revents & POLLIN)) {
                    quint64 drain;
                    ssize_t r = ::read(m_wakeFd, &drain, sizeof drain);
                    Q_UNUSED(r);
                }
                if (pr > 0 && fds[0].revents) {
                    int soErr = 0;
                    socklen_t len = sizeof soErr;
                    getsockopt(s, SOL_SOCKET, SO_ERROR, &soErr, &len);
                    if (soErr == 0)
                        ok = true;
                    else
                        lastError = errnoMessage(soErr);
                    break;
                }
            }
        } else {
            lastError = errnoMessage(errno);
        }
        if (ok)
            fd = s;
        else
            ::close(s);
    }
    freeaddrinfo(res);

    if (fd < 0) {
        *error = lastError;
        return false;
    }
    {
        QMutexLocker lock(&m_mutex);
        m_sock = fd;
        if (m_stop)
            return false; // teardown() closes it
    }
    return true;
}

void SshSession::Worker::doConnect(const QString &host, int port, int timeoutMs)
{
    if (m_done)
        return;
    if (m_session || m_sock >= 0) {
        qWarning("SshSession::connectToHost: already connected");
        return;
    }
    QString error;
    if (!connectSocket(host, port, timeoutMs, &error)) {
        finish(error);
        return;
    }

    setActivity(Blocking);
    m_session = libssh2_session_init_ex(nullptr, nullptr, nullptr, this);
    if (!m_session) {
        setActivity(Idle);
        finish(QStringLiteral("Out of memory"));
        return;
    }
    libssh2_session_callback_set2(m_session, LIBSSH2_CALLBACK_SEND,
                                  reinterpret_cast<libssh2_cb_generic *>(&sendNoSignal));
    libssh2_session_callback_set2(m_session, LIBSSH2_CALLBACK_RECV,
                                  reinterpret_cast<libssh2_cb_generic *>(&recvRetry));
    libssh2_session_callback_set2(m_session, LIBSSH2_CALLBACK_DISCONNECT,
                                  reinterpret_cast<libssh2_cb_generic *>(&disconnectCallback));
    // Same host key preference as OpenSSH, so the key (and fingerprint) the user
    // sees here matches what `ssh` shows for the same server. Unknown names are skipped.
    libssh2_session_method_pref(m_session, LIBSSH2_METHOD_HOSTKEY,
                                "ssh-ed25519,ecdsa-sha2-nistp256,ecdsa-sha2-nistp384,"
                                "ecdsa-sha2-nistp521,rsa-sha2-512,rsa-sha2-256,ssh-rsa");
    libssh2_session_set_blocking(m_session, 1);
    libssh2_session_set_timeout(m_session, qMax(1000, timeoutMs));

    const int rc = libssh2_session_handshake(m_session, m_sock);
    if (rc != 0) {
        setActivity(Idle);
        finish(rc == LIBSSH2_ERROR_SOCKET_DISCONNECT && m_serverDisconnect.isEmpty()
                   ? QStringLiteral("Connection closed by server")
                   : fatalMessage(rc, QStringLiteral("SSH handshake failed")));
        return;
    }
    libssh2_session_set_timeout(m_session, kAuthTimeoutMs);
    setActivity(Idle);

    size_t len = 0;
    int type = 0;
    const char *raw = libssh2_session_hostkey(m_session, &len, &type);
    if (!raw || len == 0) {
        finish(QStringLiteral("SSH handshake failed: the server sent no host key"));
        return;
    }
    SshHostKey key;
    const QByteArray blob(raw, int(len));
    int pos = 0;
    QByteArray typeName;
    if (readSshString(blob, &pos, &typeName) && !typeName.isEmpty()) {
        key.type = QString::fromLatin1(typeName);
    } else {
        switch (type) {
        case LIBSSH2_HOSTKEY_TYPE_RSA: key.type = QStringLiteral("ssh-rsa"); break;
        case LIBSSH2_HOSTKEY_TYPE_DSS: key.type = QStringLiteral("ssh-dss"); break;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: key.type = QStringLiteral("ecdsa-sha2-nistp256"); break;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: key.type = QStringLiteral("ecdsa-sha2-nistp384"); break;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: key.type = QStringLiteral("ecdsa-sha2-nistp521"); break;
        case LIBSSH2_HOSTKEY_TYPE_ED25519: key.type = QStringLiteral("ssh-ed25519"); break;
        default: key.type = QStringLiteral("unknown"); break;
        }
    }
    key.keyBase64 = QString::fromLatin1(blob.toBase64());
    const char *hash = libssh2_hostkey_hash(m_session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (hash) {
        QByteArray b64 = QByteArray(hash, 32).toBase64();
        while (b64.endsWith('='))
            b64.chop(1);
        key.fingerprint = QStringLiteral("SHA256:") + QString::fromLatin1(b64);
    }

    if (stopRequested())
        return;
    SshSession *session = q;
    post([session, key] {
        if (live(session))
            emit session->hostKeyReceived(key);
    });
}

// ---- authentication ---------------------------------------------------------

bool SshSession::Worker::ensureMethods(const QString &username)
{
    if (m_methodsKnown)
        return true;
    const QByteArray user = username.toUtf8();
    setActivity(Blocking);
    const char *list = libssh2_userauth_list(m_session, user.constData(), unsigned(user.size()));
    setActivity(Idle);
    if (!list) {
        if (libssh2_userauth_authenticated(m_session)) {
            m_methods.clear();
            m_methodsKnown = true;
            return true;
        }
        int code = 0;
        lastErrorText(&code);
        if (!stopRequested())
            finish(fatalMessage(code, QStringLiteral("Could not query authentication methods")));
        return false;
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    m_methods = QString::fromUtf8(list).split(QLatin1Char(','), Qt::SkipEmptyParts);
#else
    m_methods = QString::fromUtf8(list).split(QLatin1Char(','), QString::SkipEmptyParts);
#endif
    m_methodsKnown = true;
    return true;
}

void SshSession::Worker::doQueryAuth(const QString &username)
{
    if (m_done)
        return;
    if (!m_session) {
        qWarning("SshSession::queryAuthMethods: not connected");
        return;
    }
    m_methodsKnown = false; // the list depends on the user name
    if (!ensureMethods(username))
        return;
    const QStringList methods = m_methods;
    const bool already = libssh2_userauth_authenticated(m_session) != 0;
    SshSession *session = q;
    post([session, methods, already] {
        if (live(session))
            emit session->authMethodsReceived(methods, already);
    });
}

LIBSSH2_USERAUTH_KBDINT_RESPONSE_FUNC(SshSession::Worker::kbdCallback)
{
    Q_UNUSED(name);
    Q_UNUSED(name_len);
    Q_UNUSED(instruction);
    Q_UNUSED(instruction_len);
    Q_UNUSED(prompts);
    if (!abstract || !*abstract || num_prompts <= 0)
        return;
    Worker *w = static_cast<Worker *>(*abstract);
    // Answer every prompt with the password; after a few rounds answer with empty
    // strings so a server that keeps asking (e.g. for an OTP) gets a failure.
    const bool answer = ++w->m_kbdRounds <= kMaxKbdRounds;
    const QByteArray &pw = w->m_kbdPassword;
    for (int i = 0; i < num_prompts; ++i) {
        const int len = answer ? pw.size() : 0;
        // libssh2 releases each response with its free function (plain free()).
        char *text = static_cast<char *>(malloc(size_t(len) + 1));
        if (!text) {
            responses[i].text = nullptr;
            responses[i].length = 0;
            continue;
        }
        if (len)
            memcpy(text, pw.constData(), size_t(len));
        text[len] = '\0';
        responses[i].text = text;
        responses[i].length = unsigned(len);
    }
}

LIBSSH2_DISCONNECT_FUNC(SshSession::Worker::disconnectCallback)
{
    Q_UNUSED(session);
    Q_UNUSED(reason);
    Q_UNUSED(language);
    Q_UNUSED(language_len);
    if (!abstract || !*abstract)
        return;
    Worker *w = static_cast<Worker *>(*abstract);
    QString msg = message && message_len > 0 ? QString::fromUtf8(message, message_len).trimmed() : QString();
    w->m_serverDisconnect = msg.isEmpty() ? QStringLiteral("connection closed") : msg;
}

void SshSession::Worker::doAuthPassword(const QString &username, const QString &password)
{
    if (m_done)
        return;
    if (!m_session) {
        qWarning("SshSession::authenticateWithPassword: not connected");
        return;
    }
    SshSession *session = q;
    if (!ensureMethods(username))
        return;
    if (libssh2_userauth_authenticated(m_session)) {
        post([session] { if (live(session)) emit session->authenticated(); });
        return;
    }
    const bool offersPassword = m_methods.contains(QStringLiteral("password"));
    const bool offersKbd = m_methods.contains(QStringLiteral("keyboard-interactive"));
    if (!offersPassword && !offersKbd) {
        post([session] {
            if (live(session))
                emit session->authenticationFailed(
                    QStringLiteral("The server does not accept password authentication"));
        });
        return;
    }

    const QByteArray user = username.toUtf8();
    const QByteArray pw = password.toUtf8();
    int rc = LIBSSH2_ERROR_AUTHENTICATION_FAILED;
    setActivity(Blocking);
    if (offersPassword) {
        rc = libssh2_userauth_password_ex(m_session, user.constData(), unsigned(user.size()),
                                          pw.constData(), unsigned(pw.size()), nullptr);
    }
    // keyboard-interactive only when "password" isn't offered (as documented):
    // trying both would count two failures per wrong password against the
    // server's MaxAuthTries (OpenSSH default 6), i.e. the app's 3 tries.
    if (!offersPassword && offersKbd && !stopRequested()) {
        m_kbdPassword = pw;
        m_kbdRounds = 0;
        rc = libssh2_userauth_keyboard_interactive_ex(m_session, user.constData(), unsigned(user.size()),
                                                      &kbdCallback);
        m_kbdPassword.fill('\0');
        m_kbdPassword.clear();
    }
    setActivity(Idle);

    if (rc == 0) {
        post([session] { if (live(session)) emit session->authenticated(); });
        return;
    }
    authFailedOrClosed(rc, rc == LIBSSH2_ERROR_PASSWORD_EXPIRED
                               ? QStringLiteral("The password has expired; change it with another SSH client")
                               : QStringLiteral("Wrong user name or password"));
}

void SshSession::Worker::doAuthKey(const QString &username, const QByteArray &privateKey,
                                   const QString &passphrase)
{
    if (m_done)
        return;
    if (!m_session) {
        qWarning("SshSession::authenticateWithKey: not connected");
        return;
    }
    SshSession *session = q;
    if (!ensureMethods(username))
        return;
    if (libssh2_userauth_authenticated(m_session)) {
        post([session] { if (live(session)) emit session->authenticated(); });
        return;
    }

    auto reject = [this, session](const QString &message) {
        post([session, message] {
            if (live(session))
                emit session->authenticationFailed(message);
        });
    };

    if (m_methodsKnown && !m_methods.isEmpty() && !m_methods.contains(QStringLiteral("publickey"))) {
        reject(QStringLiteral("The server does not accept key authentication"));
        return;
    }
    const KeyKind kind = inspectPrivateKey(privateKey);
    switch (kind) {
    case KeyKind::NotAKey:
        reject(QStringLiteral("Not a private key (expected OpenSSH or PEM format)"));
        return;
    case KeyKind::PublicKey:
        reject(QStringLiteral("This is a public key; the private key is needed"));
        return;
    case KeyKind::PuttyKey:
        reject(QStringLiteral("PuTTY keys are not supported; convert the key to OpenSSH format"));
        return;
    case KeyKind::Encrypted:
        if (passphrase.isEmpty()) {
            reject(QStringLiteral("The private key is encrypted; enter its passphrase"));
            return;
        }
        break;
    case KeyKind::Plain:
        break;
    }

    const QByteArray user = username.toUtf8();
    const QByteArray key = privateKey.trimmed() + '\n'; // NUL-terminated by QByteArray
    QByteArray pass = passphrase.toUtf8();              // never NULL: libssh2 strlen()s it
    setActivity(Blocking);
    const int rc = libssh2_userauth_publickey_frommemory(m_session, user.constData(), size_t(user.size()),
                                                         nullptr, 0, key.constData(), size_t(key.size()),
                                                         pass.constData());
    setActivity(Idle);
    pass.fill('\0');

    if (rc == 0) {
        post([session] { if (live(session)) emit session->authenticated(); });
        return;
    }
    QString message;
    switch (rc) {
    case LIBSSH2_ERROR_FILE:
    case LIBSSH2_ERROR_KEYFILE_AUTH_FAILED:
        message = kind == KeyKind::Encrypted ? QStringLiteral("Wrong passphrase for the private key")
                                             : QStringLiteral("Unreadable or unsupported private key");
        break;
    case LIBSSH2_ERROR_METHOD_NOT_SUPPORTED:
    case LIBSSH2_ERROR_ALGO_UNSUPPORTED:
        message = QStringLiteral("Unsupported key type");
        break;
    default:
        message = QStringLiteral("The server did not accept the key");
        break;
    }
    authFailedOrClosed(rc, message);
}

// ---- shell ------------------------------------------------------------------

void SshSession::Worker::doStartShell(int cols, int rows, const QString &term)
{
    if (m_done)
        return;
    if (!m_session) {
        qWarning("SshSession::startShell: not connected");
        return;
    }
    if (m_channel) {
        qWarning("SshSession::startShell: shell already started");
        return;
    }
    setActivity(Blocking);
    m_channel = libssh2_channel_open_ex(m_session, "session", 7, kChannelWindow, kChannelPacket, nullptr, 0);
    if (!m_channel) {
        int code = 0;
        lastErrorText(&code);
        setActivity(Idle);
        finish(fatalMessage(code, QStringLiteral("Could not open a shell")));
        return;
    }
    const QByteArray termName = (term.isEmpty() ? QStringLiteral("xterm-256color") : term).toLatin1();
    m_ptyCols = qMax(1, cols);
    m_ptyRows = qMax(1, rows);
    int rc = libssh2_channel_request_pty_ex(m_channel, termName.constData(), unsigned(termName.size()),
                                            nullptr, 0, m_ptyCols, m_ptyRows, 0, 0);
    if (rc != 0) {
        setActivity(Idle);
        finish(fatalMessage(rc, QStringLiteral("The server refused the terminal")));
        return;
    }
    rc = libssh2_channel_process_startup(m_channel, "shell", 5, nullptr, 0);
    if (rc != 0) {
        setActivity(Idle);
        finish(fatalMessage(rc, QStringLiteral("Could not start the shell")));
        return;
    }
    {
        QMutexLocker lock(&m_mutex);
        if (m_stop) {
            m_activity = Idle;
            return;
        }
        m_shellActive = true;
        m_activity = IoLoop;
    }
    SshSession *session = q;
    post([session] { if (live(session)) emit session->shellStarted(); });
    ioLoop();
    setActivity(Idle);
}

void SshSession::Worker::ioLoop()
{
    LIBSSH2_SESSION *s = m_session;
    LIBSSH2_CHANNEL *ch = m_channel;
    libssh2_session_set_blocking(s, 0);
    libssh2_keepalive_config(s, 1, kKeepaliveSeconds);

    QByteArray outgoing;
    int outOffset = 0;
    int curCols = m_ptyCols, curRows = m_ptyRows;
    bool resizeInFlight = false;
    int rfCols = 0, rfRows = 0;
    static const int kBufSize = 32 * 1024;
    QByteArray bufStore(kBufSize, Qt::Uninitialized);
    char *buf = bufStore.data();
    QString error;
    SshSession *session = q;

    for (;;) {
        if (stopRequested())
            break;

        // 1. Output: stdout (0) and stderr (1).
        QByteArray chunk;
        bool budgetHit = false;
        bool channelGone = false;
        bool failed = false;
        for (int stream = 0; stream < 2 && !failed; ++stream) {
            for (;;) {
                const ssize_t n = libssh2_channel_read_ex(ch, stream, buf, kBufSize);
                if (n > 0) {
                    chunk.append(buf, int(n));
                    if (chunk.size() >= kReadBudget) {
                        budgetHit = true;
                        break;
                    }
                } else if (n == 0 || n == LIBSSH2_ERROR_EAGAIN) {
                    break;
                } else if (n == LIBSSH2_ERROR_CHANNEL_CLOSED || n == LIBSSH2_ERROR_CHANNEL_EOF_SENT) {
                    channelGone = true;
                    break;
                } else {
                    error = fatalMessage(int(n), QString());
                    failed = true;
                    break;
                }
            }
        }
        if (!chunk.isEmpty()) {
            post([session, chunk] {
                if (live(session))
                    emit session->dataReceived(chunk);
            });
        }
        if (failed)
            break;

        // 2. Input: queued writes. On EAGAIN libssh2 wants the same bytes again,
        // which holds because we only ever append behind outOffset.
        {
            QMutexLocker lock(&m_mutex);
            if (!m_pendingWrite.isEmpty()) {
                outgoing.append(m_pendingWrite);
                m_pendingWrite.clear();
            }
        }
        while (outOffset < outgoing.size()) {
            const ssize_t n = libssh2_channel_write_ex(ch, 0, outgoing.constData() + outOffset,
                                                       size_t(outgoing.size() - outOffset));
            if (n > 0) {
                outOffset += int(n);
            } else if (n == 0 || n == LIBSSH2_ERROR_EAGAIN) {
                break;
            } else if (n == LIBSSH2_ERROR_CHANNEL_CLOSED || n == LIBSSH2_ERROR_CHANNEL_EOF_SENT) {
                channelGone = true;
                break;
            } else {
                error = fatalMessage(int(n), QString());
                failed = true;
                break;
            }
        }
        if (failed)
            break;
        if (outOffset >= outgoing.size()) {
            outgoing.clear();
            outOffset = 0;
        } else if (outOffset > 64 * 1024) {
            outgoing.remove(0, outOffset);
            outOffset = 0;
        }

        // 3. Window size: only the latest one. A request that got EAGAIN must be
        // finished (libssh2 keeps its packet) before a newer size is sent.
        for (int round = 0; round < 2 && !failed; ++round) {
            if (!resizeInFlight) {
                QMutexLocker lock(&m_mutex);
                if (m_resizePending) {
                    m_resizePending = false;
                    if (m_pendingCols != curCols || m_pendingRows != curRows) {
                        rfCols = m_pendingCols;
                        rfRows = m_pendingRows;
                        resizeInFlight = true;
                    }
                }
            }
            if (!resizeInFlight)
                break;
            const int rc = libssh2_channel_request_pty_size_ex(ch, rfCols, rfRows, 0, 0);
            if (rc == LIBSSH2_ERROR_EAGAIN)
                break;
            resizeInFlight = false;
            if (rc == 0) {
                curCols = rfCols;
                curRows = rfRows;
            } else if (isConnectionError(rc)) {
                error = fatalMessage(rc, QString());
                failed = true;
            }
        }
        if (failed)
            break;

        // 4. End of the remote shell.
        if (channelGone || (!budgetHit && libssh2_channel_eof(ch) == 1))
            break;

        // 5. Keepalive.
        int nextKeepalive = kKeepaliveSeconds;
        const int krc = libssh2_keepalive_send(s, &nextKeepalive);
        if (krc != 0 && krc != LIBSSH2_ERROR_EAGAIN) {
            error = fatalMessage(krc, QString());
            break;
        }

        // 6. Sleep until the socket or the wake eventfd has something for us.
        if (budgetHit)
            continue; // more output is already waiting in libssh2
        const int dirs = libssh2_session_block_directions(s);
        struct pollfd fds[2];
        fds[0].fd = m_sock;
        fds[0].events = POLLIN;
        if (dirs & LIBSSH2_SESSION_BLOCK_OUTBOUND)
            fds[0].events |= POLLOUT;
        fds[0].revents = 0;
        fds[1].fd = m_wakeFd;
        fds[1].events = POLLIN;
        fds[1].revents = 0;
        const int timeoutMs = qBound(1, nextKeepalive, kKeepaliveSeconds) * 1000;
        const int pr = ::poll(fds, m_wakeFd >= 0 ? 2 : 1, timeoutMs);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            error = errnoMessage(errno);
            break;
        }
        if (fds[0].revents & POLLNVAL) {
            error = QStringLiteral("Connection lost");
            break;
        }
        if ((fds[0].revents & (POLLERR | POLLHUP)) && !(fds[0].revents & POLLIN)) {
            int soErr = 0;
            socklen_t len = sizeof soErr;
            getsockopt(m_sock, SOL_SOCKET, SO_ERROR, &soErr, &len);
            error = (soErr && soErr != EPIPE) ? QStringLiteral("Connection lost: %1").arg(errnoMessage(soErr))
                                              : QStringLiteral("Connection lost");
            break;
        }
        if (m_wakeFd >= 0 && (fds[1].revents & POLLIN)) {
            quint64 drain;
            ssize_t r = ::read(m_wakeFd, &drain, sizeof drain);
            Q_UNUSED(r);
        }
    }

    finish(error);
}

// ---- teardown ---------------------------------------------------------------

void SshSession::Worker::teardown()
{
    // Worker thread, after the last task. Bounded: blocking mode with a short
    // timeout, and a dead or shut-down socket makes every call fail at once.
    if (m_session) {
        libssh2_session_set_blocking(m_session, 1);
        libssh2_session_set_timeout(m_session, kTeardownTimeoutMs);
        if (m_serverDisconnect.isEmpty())
            libssh2_session_disconnect_ex(m_session, SSH_DISCONNECT_BY_APPLICATION, "Closed by user", "");
        {
            // Nothing more needs to go over the wire; this keeps the frees below
            // from waiting for channel-close replies.
            QMutexLocker lock(&m_mutex);
            if (m_sock >= 0)
                ::shutdown(m_sock, SHUT_RDWR);
        }
        if (m_channel) {
            libssh2_channel_free(m_channel);
            m_channel = nullptr;
        }
        libssh2_session_free(m_session);
        m_session = nullptr;
    }
    QMutexLocker lock(&m_mutex);
    if (m_sock >= 0) {
        ::close(m_sock);
        m_sock = -1;
    }
    m_activity = Idle;
    m_shellActive = false;
    m_pendingWrite.fill('\0');
    m_pendingWrite.clear();
}

// ---- SshSession -------------------------------------------------------------

SshSession::SshSession(QObject *parent)
    : QObject(parent)
{
    globalInit();
    Relay *relay = new Relay(this);
    m_worker = new Worker(this, relay);
    m_thread = m_worker;
    m_worker->start();
}

SshSession::~SshSession()
{
    m_closed = true; // no signals from a dying object
    if (!m_worker)
        return;
    m_worker->detachRelay();
    m_worker->requestStop();
    // Normally the worker says goodbye to the server and exits quickly. If it is
    // stuck (unresponsive peer), cut the socket; if it is still stuck after that
    // (a DNS lookup can't be interrupted), let it finish and delete itself later.
    // A DNS lookup can take as long as the resolver likes: don't freeze the UI for it.
    const bool resolving = m_worker->m_resolving;
    if (resolving || !m_worker->wait(1000)) {
        m_worker->forceShutdown();
        if (resolving || !m_worker->wait(3000)) {
            qWarning("SshSession: worker thread still busy; detaching it");
            Worker *w = m_worker;
            QObject::connect(w, &QThread::finished, w, &QObject::deleteLater);
            if (w->isFinished())
                delete w;
            m_worker = nullptr;
            m_thread = nullptr;
            return;
        }
    }
    delete m_worker;
    m_worker = nullptr;
    m_thread = nullptr;
}

void SshSession::connectToHost(const QString &host, int port, int timeoutMs)
{
    if (m_closed)
        return;
    Worker *w = m_worker;
    m_worker->enqueue([w, host, port, timeoutMs] { w->doConnect(host, port, timeoutMs); });
}

void SshSession::queryAuthMethods(const QString &username)
{
    if (m_closed)
        return;
    Worker *w = m_worker;
    m_worker->enqueue([w, username] { w->doQueryAuth(username); });
}

void SshSession::authenticateWithPassword(const QString &username, const QString &password)
{
    if (m_closed)
        return;
    Worker *w = m_worker;
    m_worker->enqueue([w, username, password] { w->doAuthPassword(username, password); });
}

void SshSession::authenticateWithKey(const QString &username, const QByteArray &privateKey,
                                     const QString &passphrase)
{
    if (m_closed)
        return;
    Worker *w = m_worker;
    m_worker->enqueue([w, username, privateKey, passphrase] { w->doAuthKey(username, privateKey, passphrase); });
}

void SshSession::startShell(int cols, int rows, const QString &termType)
{
    if (m_closed)
        return;
    Worker *w = m_worker;
    m_worker->enqueue([w, cols, rows, termType] { w->doStartShell(cols, rows, termType); });
}

void SshSession::write(const QByteArray &data)
{
    if (m_closed || data.isEmpty())
        return;
    m_worker->queueWrite(data);
}

void SshSession::resize(int cols, int rows)
{
    if (m_closed || cols <= 0 || rows <= 0)
        return;
    m_worker->queueResize(cols, rows);
}

void SshSession::disconnectFromHost()
{
    if (m_closed || m_worker->stopRequested())
        return;
    m_worker->m_ownerStopped = true;
    m_worker->requestStop();
    // closed("") comes right away (queued), without waiting for the worker to
    // finish saying goodbye to the server.
    m_worker->postClosedFromOwner(QString());
}

bool SshSession::isClosed() const
{
    return m_closed;
}
