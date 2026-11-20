#ifndef SSHSESSION_H
#define SSHSESSION_H

#include <QByteArray>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>

// The server's host key, as reported after the SSH handshake.
struct SshHostKey
{
    QString type;        // e.g. "ssh-ed25519", "ecdsa-sha2-nistp256", "ssh-rsa"
    QString keyBase64;   // the raw public key blob, base64 (as in known_hosts)
    QString fingerprint; // "SHA256:<base64 without padding>", same as OpenSSH prints
};
Q_DECLARE_METATYPE(SshHostKey)

// One SSH connection with one interactive shell channel, on top of libssh2.
//
// All libssh2 work runs on a private worker thread owned by this object, because a
// libssh2 session is not thread-safe. Every public method may be called from the
// thread that owns the SshSession (the GUI thread) and returns at once; results come
// back as signals, delivered in the owner's thread.
//
// Call the steps in order: connectToHost -> (check the host key) -> queryAuthMethods
// -> authenticate* (may be repeated after authenticationFailed) -> startShell.
// After shellStarted, write() and resize() feed the channel and dataReceived carries
// the output.
//
// closed() is emitted exactly once, when the session is finished for any reason:
// a connection error in any phase, the remote shell exiting, or disconnectFromHost().
// After closed() the object is inert; make a new SshSession to reconnect.
class SshSession : public QObject
{
    Q_OBJECT
public:
    explicit SshSession(QObject *parent = nullptr);
    ~SshSession() override; // disconnects and joins the worker thread

    // TCP connect (with timeout), then the SSH handshake. -> hostKeyReceived | closed
    void connectToHost(const QString &host, int port, int timeoutMs = 15000);

    // "none" auth to learn the methods the server accepts. -> authMethodsReceived | closed
    void queryAuthMethods(const QString &username);

    // "password", falling back to "keyboard-interactive" (every prompt answered with
    // the password) when the server doesn't offer "password".
    // -> authenticated | authenticationFailed | closed
    void authenticateWithPassword(const QString &username, const QString &password);

    // "publickey" from an in-memory OpenSSH or PEM private key (RSA, ECDSA, Ed25519),
    // with an optional passphrase. -> authenticated | authenticationFailed | closed
    void authenticateWithKey(const QString &username, const QByteArray &privateKey,
                             const QString &passphrase = QString());

    // Opens a PTY of the given size and a shell on it. -> shellStarted | closed
    void startShell(int cols, int rows, const QString &termType = QStringLiteral("xterm-256color"));

    // Queue bytes for the shell's stdin. Ignored before shellStarted or after closed.
    void write(const QByteArray &data);

    // Change the PTY size. Coalesced: only the latest size is sent.
    void resize(int cols, int rows);

    // Close the channel and the connection. -> closed("") unless already closed.
    void disconnectFromHost();

    bool isClosed() const;

signals:
    void hostKeyReceived(const SshHostKey &key);
    // methods: e.g. ("publickey", "password", "keyboard-interactive").
    // alreadyAuthenticated: the server accepted "none" (no further auth needed).
    void authMethodsReceived(const QStringList &methods, bool alreadyAuthenticated);
    void authenticated();
    // The credentials were refused (wrong password, key not accepted, wrong passphrase,
    // unreadable key). The connection is still up: another authenticate* may follow.
    void authenticationFailed(const QString &message);
    void shellStarted();
    void dataReceived(const QByteArray &data); // stdout and stderr, in arrival order
    // error is empty for a clean end (shell exited, or disconnectFromHost()); otherwise
    // a short human-readable message such as "Connection refused" or "Connection timed out".
    void closed(const QString &error);

private:
    class Worker;
    Worker *m_worker = nullptr;
    class QThread *m_thread = nullptr;
    bool m_closed = false;
};

#endif // SSHSESSION_H
