#ifndef SECRETSTORE_H
#define SECRETSTORE_H

#include <QHash>
#include <QString>

// Secrets for one host.
struct HostSecrets
{
    QString password;
    QString privateKey; // OpenSSH or PEM text
    QString passphrase;

    bool isEmpty() const { return password.isEmpty() && privateKey.isEmpty() && passphrase.isEmpty(); }
};

// Passwords, private keys and passphrases, keyed by host id.
//
// Sailfish OS has no keychain service that third-party apps can rely on (the Sailfish
// Secrets daemon isn't installed on the devices), so the secrets live in their own
// file, secrets.json, readable only by the owner (0600, directory 0700). Sailjail
// keeps the app's data directory out of reach of other sandboxed apps. The host list
// itself never holds secrets.
class SecretStore
{
public:
    explicit SecretStore(const QString &filePath);

    HostSecrets secrets(const QString &hostId) const;
    void setSecrets(const QString &hostId, const HostSecrets &secrets);
    void setPassword(const QString &hostId, const QString &password);
    void setPrivateKey(const QString &hostId, const QString &privateKey, const QString &passphrase);
    void setPassphrase(const QString &hostId, const QString &passphrase);
    void remove(const QString &hostId);

private:
    void load();
    void save() const;

    QString m_path;
    QHash<QString, HostSecrets> m_secrets;
};

#endif // SECRETSTORE_H
