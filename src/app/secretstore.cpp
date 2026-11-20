#include "app/secretstore.h"
#include "app/storage.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

SecretStore::SecretStore(const QString &filePath)
    : m_path(filePath)
{
    load();
}

HostSecrets SecretStore::secrets(const QString &hostId) const
{
    return m_secrets.value(hostId);
}

void SecretStore::setSecrets(const QString &hostId, const HostSecrets &secrets)
{
    if (secrets.isEmpty())
        m_secrets.remove(hostId);
    else
        m_secrets.insert(hostId, secrets);
    save();
}

void SecretStore::setPassword(const QString &hostId, const QString &password)
{
    HostSecrets s = secrets(hostId);
    s.password = password;
    setSecrets(hostId, s);
}

void SecretStore::setPrivateKey(const QString &hostId, const QString &privateKey, const QString &passphrase)
{
    HostSecrets s = secrets(hostId);
    s.privateKey = privateKey;
    s.passphrase = passphrase;
    setSecrets(hostId, s);
}

void SecretStore::setPassphrase(const QString &hostId, const QString &passphrase)
{
    HostSecrets s = secrets(hostId);
    s.passphrase = passphrase;
    setSecrets(hostId, s);
}

void SecretStore::remove(const QString &hostId)
{
    if (m_secrets.remove(hostId))
        save();
}

void SecretStore::load()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject all = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        HostSecrets s;
        s.password = o.value(QStringLiteral("password")).toString();
        s.privateKey = o.value(QStringLiteral("privateKey")).toString();
        s.passphrase = o.value(QStringLiteral("passphrase")).toString();
        if (!s.isEmpty())
            m_secrets.insert(it.key(), s);
    }
}

void SecretStore::save() const
{
    QJsonObject all;
    for (auto it = m_secrets.constBegin(); it != m_secrets.constEnd(); ++it) {
        QJsonObject o;
        if (!it->password.isEmpty())
            o.insert(QStringLiteral("password"), it->password);
        if (!it->privateKey.isEmpty())
            o.insert(QStringLiteral("privateKey"), it->privateKey);
        if (!it->passphrase.isEmpty())
            o.insert(QStringLiteral("passphrase"), it->passphrase);
        all.insert(it.key(), o);
    }
    Storage::writePrivateFile(m_path, QJsonDocument(all).toJson(QJsonDocument::Compact));
}
