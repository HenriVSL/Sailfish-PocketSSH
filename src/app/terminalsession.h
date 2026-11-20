#ifndef TERMINALSESSION_H
#define TERMINALSESSION_H

#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include "app/hostmodel.h"
#include "app/secretstore.h"
#include "ssh/sshsession.h"

class KnownHosts;
class Terminal;

// One tab: a Terminal wired to an SshSession, and the connect flow around it:
// connect -> verify host key (KnownHosts; a prompt if unknown or changed) ->
// authenticate (saved secrets; password/passphrase prompt with up to 3 tries and an
// optional "save") -> shell. The Terminal lives as long as the tab, so reconnecting
// keeps the scrollback.
class TerminalSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString hostId READ hostId CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString address READ address NOTIFY addressChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    // Why the session closed, empty for a clean close or a cancelled connect.
    Q_PROPERTY(QString closeReason READ closeReason NOTIFY stateChanged)
    Q_PROPERTY(Terminal *terminal READ terminal CONSTANT)
    Q_PROPERTY(Prompt prompt READ prompt NOTIFY promptChanged)
    // HostKeyPrompt: { address, keyType, fingerprint, previousFingerprint (if changed) }
    // PasswordPrompt / PassphrasePrompt: { address, message (after a failed try) }
    Q_PROPERTY(QVariantMap promptData READ promptData NOTIFY promptChanged)

public:
    enum State { Connecting, Connected, Closed };
    Q_ENUM(State)
    enum Prompt { NoPrompt, HostKeyPrompt, PasswordPrompt, PassphrasePrompt };
    Q_ENUM(Prompt)

    TerminalSession(const Host &host, HostModel *hosts, SecretStore *secrets, KnownHosts *knownHosts,
                    QObject *parent = nullptr);
    ~TerminalSession() override;

    QString hostId() const { return m_host.id; }
    QString title() const;
    QString address() const { return m_host.address(); }
    State state() const { return m_state; }
    QString closeReason() const { return m_closeReason; }
    Terminal *terminal() const { return m_terminal; }
    Prompt prompt() const { return m_prompt; }
    QVariantMap promptData() const { return m_promptData; }

    // Starts connecting (again). Picks up edits made to the host since the last time.
    Q_INVOKABLE void reconnect();
    // Disconnects; the tab stays (Closed) until the manager removes it.
    Q_INVOKABLE void disconnectFromHost();
    Q_INVOKABLE void answerHostKey(bool trust);
    // An empty secret cancels.
    Q_INVOKABLE void answerPassword(const QString &secret, bool remember);

signals:
    void titleChanged();
    void addressChanged();
    void stateChanged();
    void promptChanged();
    // A bell from the remote program (the page flashes).
    void bell();
    // OSC 52 from the remote program: the text was put on the clipboard.
    void copiedToClipboard();

private slots:
    void onHostKey(const SshHostKey &key);
    void onAuthMethods(const QStringList &methods, bool alreadyAuthenticated);
    void onAuthenticated();
    void onAuthenticationFailed(const QString &message);
    void onShellStarted();
    void onData(const QByteArray &data);
    void onClosed(const QString &error);
    void onTerminalOutput(const QByteArray &data);
    void onTerminalSizeChanged();
    void onClipboardWrite(const QString &text);

private:
    void start();
    void tryAuthentication();
    void askSecret(Prompt kind, const QString &message);
    void setPrompt(Prompt prompt, const QVariantMap &data);
    void setState(State state);
    void fail(const QString &message);
    void printLine(const QString &line, const char *sgr = nullptr);

    Host m_host;
    HostModel *m_hosts;
    SecretStore *m_secrets;
    KnownHosts *m_knownHosts;
    Terminal *m_terminal;
    QPointer<SshSession> m_ssh;
    SshHostKey m_pendingKey;

    State m_state = Closed;
    QString m_closeReason;
    QString m_pendingFailure; // reason given to fail(), reported when the session has closed
    Prompt m_prompt = NoPrompt;
    QVariantMap m_promptData;

    // Authentication state for one connect.
    QStringList m_methods;
    HostSecrets m_saved;
    QString m_tryingSecret;
    bool m_rememberSecret = false;
    int m_attempts = 0;      // failed tries with secrets the user typed
    bool m_triedSaved = false;  // the saved secret has been used this connect
    bool m_tryingSaved = false; // the try in flight uses the saved secret
};

#endif // TERMINALSESSION_H
