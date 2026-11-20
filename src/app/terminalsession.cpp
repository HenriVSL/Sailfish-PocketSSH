#include "app/terminalsession.h"
#include "app/keyinfo.h"
#include "app/knownhosts.h"
#include "terminal/terminal.h"

#include <QClipboard>
#include <QGuiApplication>

TerminalSession::TerminalSession(const Host &host, HostModel *hosts, SecretStore *secrets,
                                 KnownHosts *knownHosts, QObject *parent)
    : QObject(parent)
    , m_host(host)
    , m_hosts(hosts)
    , m_secrets(secrets)
    , m_knownHosts(knownHosts)
    , m_terminal(new Terminal(this))
{
    connect(m_terminal, &Terminal::output, this, &TerminalSession::onTerminalOutput);
    connect(m_terminal, &Terminal::sizeChanged, this, &TerminalSession::onTerminalSizeChanged);
    connect(m_terminal, &Terminal::titleChanged, this, &TerminalSession::titleChanged);
    connect(m_terminal, &Terminal::bell, this, &TerminalSession::bell);
    connect(m_terminal, &Terminal::clipboardWriteRequested, this, &TerminalSession::onClipboardWrite);
    start();
}

TerminalSession::~TerminalSession()
{
    if (m_ssh) {
        m_ssh->disconnect(this);
        delete m_ssh.data();
    }
}

QString TerminalSession::title() const
{
    const QString t = m_terminal->title().trimmed();
    return t.isEmpty() ? m_host.displayName() : t;
}

void TerminalSession::reconnect()
{
    if (m_state == Closed)
        start();
}

void TerminalSession::disconnectFromHost()
{
    if (m_state != Closed)
        fail(QString());
}

void TerminalSession::start()
{
    // Pick up edits to the host (address, user, auth method) made since the last connect.
    if (m_hosts->contains(m_host.id)) {
        m_host = m_hosts->host(m_host.id);
        emit addressChanged();
        emit titleChanged();
    }
    m_closeReason.clear();
    m_pendingFailure.clear();
    m_attempts = 0;
    m_triedSaved = false;
    m_tryingSaved = false;
    m_tryingSecret.clear();
    m_rememberSecret = false;
    setPrompt(NoPrompt, QVariantMap());
    setState(Connecting);
    printLine(tr("Connecting to %1…").arg(m_host.address()), "2");

    m_ssh = new SshSession(this);
    connect(m_ssh.data(), &SshSession::hostKeyReceived, this, &TerminalSession::onHostKey);
    connect(m_ssh.data(), &SshSession::authMethodsReceived, this, &TerminalSession::onAuthMethods);
    connect(m_ssh.data(), &SshSession::authenticated, this, &TerminalSession::onAuthenticated);
    connect(m_ssh.data(), &SshSession::authenticationFailed, this, &TerminalSession::onAuthenticationFailed);
    connect(m_ssh.data(), &SshSession::shellStarted, this, &TerminalSession::onShellStarted);
    connect(m_ssh.data(), &SshSession::dataReceived, this, &TerminalSession::onData);
    connect(m_ssh.data(), &SshSession::closed, this, &TerminalSession::onClosed);
    m_ssh->connectToHost(m_host.hostname, m_host.port);
}

void TerminalSession::onHostKey(const SshHostKey &key)
{
    if (sender() != m_ssh.data())
        return;
    KnownHosts::Entry previous;
    const KnownHosts::Result result = m_knownHosts->check(m_host.hostname, m_host.port, key.type, key.keyBase64, &previous);
    if (result == KnownHosts::Trusted) {
        m_ssh->queryAuthMethods(m_host.username);
        return;
    }
    m_pendingKey = key;
    QVariantMap data;
    data.insert(QStringLiteral("address"), m_host.hostname + QLatin1Char(':') + QString::number(m_host.port));
    data.insert(QStringLiteral("keyType"), key.type);
    data.insert(QStringLiteral("fingerprint"), key.fingerprint);
    if (result == KnownHosts::Changed)
        data.insert(QStringLiteral("previousFingerprint"), previous.fingerprint);
    setPrompt(HostKeyPrompt, data);
}

void TerminalSession::answerHostKey(bool trust)
{
    if (m_prompt != HostKeyPrompt)
        return;
    setPrompt(NoPrompt, QVariantMap());
    if (!trust || !m_ssh) {
        fail(QString());
        return;
    }
    KnownHosts::Entry entry;
    entry.type = m_pendingKey.type;
    entry.keyBase64 = m_pendingKey.keyBase64;
    entry.fingerprint = m_pendingKey.fingerprint;
    m_knownHosts->trust(m_host.hostname, m_host.port, entry);
    m_ssh->queryAuthMethods(m_host.username);
}

void TerminalSession::onAuthMethods(const QStringList &methods, bool alreadyAuthenticated)
{
    if (sender() != m_ssh.data())
        return;
    if (alreadyAuthenticated) {
        onAuthenticated();
        return;
    }
    m_methods = methods;
    m_saved = m_secrets->secrets(m_host.id);
    tryAuthentication();
}

void TerminalSession::tryAuthentication()
{
    if (m_host.authMethod == QLatin1String("privateKey")) {
        if (m_saved.privateKey.isEmpty()) {
            fail(tr("No private key saved for this host. Edit the host to add one."));
            return;
        }
        if (!m_methods.isEmpty() && !m_methods.contains(QStringLiteral("publickey"))) {
            fail(tr("The server doesn't accept keys (it offers: %1).").arg(m_methods.join(QStringLiteral(", "))));
            return;
        }
        if (m_tryingSecret.isEmpty() && !m_triedSaved) {
            m_triedSaved = true;
            m_tryingSecret = m_saved.passphrase;
            m_tryingSaved = !m_tryingSecret.isEmpty();
        }
        if (m_tryingSecret.isEmpty() && KeyInfo::inspect(m_saved.privateKey).encrypted) {
            askSecret(PassphrasePrompt, QString());
            return;
        }
        m_ssh->authenticateWithKey(m_host.username, m_saved.privateKey.toUtf8(), m_tryingSecret);
        return;
    }

    if (!m_methods.isEmpty() && !m_methods.contains(QStringLiteral("password"))
            && !m_methods.contains(QStringLiteral("keyboard-interactive"))) {
        fail(tr("The server doesn't accept passwords (it offers: %1).").arg(m_methods.join(QStringLiteral(", "))));
        return;
    }
    if (m_tryingSecret.isEmpty() && !m_triedSaved) {
        m_triedSaved = true;
        m_tryingSecret = m_saved.password;
        m_tryingSaved = !m_tryingSecret.isEmpty();
    }
    if (m_tryingSecret.isEmpty()) {
        askSecret(PasswordPrompt, m_attempts > 0 || !m_saved.password.isEmpty() ? tr("Wrong password, try again.") : QString());
        return;
    }
    m_ssh->authenticateWithPassword(m_host.username, m_tryingSecret);
}

void TerminalSession::askSecret(Prompt kind, const QString &message)
{
    QVariantMap data;
    data.insert(QStringLiteral("address"), m_host.address());
    if (!message.isEmpty())
        data.insert(QStringLiteral("message"), message);
    setPrompt(kind, data);
}

void TerminalSession::answerPassword(const QString &secret, bool remember)
{
    if (m_prompt != PasswordPrompt && m_prompt != PassphrasePrompt)
        return;
    setPrompt(NoPrompt, QVariantMap());
    if (secret.isEmpty() || !m_ssh) {
        fail(QString());
        return;
    }
    m_tryingSecret = secret;
    m_rememberSecret = remember;
    tryAuthentication();
}

void TerminalSession::onAuthenticationFailed(const QString &message)
{
    if (sender() != m_ssh.data())
        return;
    // The automatic try with a saved secret doesn't use up one of the user's three tries.
    if (!m_tryingSaved)
        ++m_attempts;
    m_tryingSaved = false;
    m_tryingSecret.clear();
    m_rememberSecret = false;
    const bool key = m_host.authMethod == QLatin1String("privateKey");
    // An unencrypted key that the server refuses won't work on a second try either.
    if (key && !KeyInfo::inspect(m_saved.privateKey).encrypted) {
        fail(message);
        return;
    }
    if (m_attempts >= 3) {
        fail(tr("Too many attempts."));
        return;
    }
    if (key)
        askSecret(PassphrasePrompt, message);
    else
        tryAuthentication();
}

void TerminalSession::onAuthenticated()
{
    // Not for a host deleted while connecting: nothing could remove the secret again.
    if (m_rememberSecret && !m_tryingSecret.isEmpty() && m_hosts->contains(m_host.id)) {
        if (m_host.authMethod == QLatin1String("privateKey"))
            m_secrets->setPassphrase(m_host.id, m_tryingSecret);
        else
            m_hosts->setPassword(m_host.id, m_tryingSecret);
    }
    m_tryingSecret.clear();
    m_ssh->startShell(qMax(m_terminal->columns(), 10), qMax(m_terminal->rows(), 4));
}

void TerminalSession::onShellStarted()
{
    if (sender() != m_ssh.data())
        return;
    setState(Connected);
    // The view may have changed size while the shell was being set up.
    m_ssh->resize(m_terminal->columns(), m_terminal->rows());
}

void TerminalSession::onData(const QByteArray &data)
{
    if (sender() == m_ssh.data())
        m_terminal->feed(data);
}

void TerminalSession::onClosed(const QString &error)
{
    if (sender() != m_ssh.data())
        return;
    m_ssh->deleteLater();
    m_ssh = nullptr;
    setPrompt(NoPrompt, QVariantMap());

    const QString reason = error.isEmpty() ? m_pendingFailure : error;
    m_pendingFailure.clear();
    if (!reason.isEmpty())
        printLine(reason, "31");
    else if (m_state == Connected)
        printLine(tr("Connection closed."), "2");
    m_closeReason = reason;
    setState(Closed);
}

void TerminalSession::fail(const QString &message)
{
    m_pendingFailure = message;
    if (m_ssh)
        m_ssh->disconnectFromHost(); // -> onClosed
}

void TerminalSession::onTerminalOutput(const QByteArray &data)
{
    if (m_state == Connected && m_ssh)
        m_ssh->write(data);
}

void TerminalSession::onTerminalSizeChanged()
{
    if (m_state == Connected && m_ssh)
        m_ssh->resize(m_terminal->columns(), m_terminal->rows());
}

void TerminalSession::onClipboardWrite(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
    emit copiedToClipboard();
}

void TerminalSession::setPrompt(Prompt prompt, const QVariantMap &data)
{
    if (prompt == m_prompt && data == m_promptData)
        return;
    m_prompt = prompt;
    m_promptData = data;
    emit promptChanged();
}

void TerminalSession::setState(State state)
{
    if (state == m_state)
        return;
    m_state = state;
    emit stateChanged();
}

void TerminalSession::printLine(const QString &line, const char *sgr)
{
    QString text = QStringLiteral("\r\n");
    if (sgr)
        text += QStringLiteral("\x1b[") + QLatin1String(sgr) + QLatin1Char('m');
    text += line + QStringLiteral("\x1b[0m\r\n");
    m_terminal->feed(text.toUtf8());
}
