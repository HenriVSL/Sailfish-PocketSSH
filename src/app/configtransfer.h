#ifndef CONFIGTRANSFER_H
#define CONFIGTRANSFER_H

#include <QObject>
#include <QStringList>
#include <QVariantMap>

class HostModel;
class SecretStore;

// Export and import of hosts in the Pocket Shell JSON format ("pocket-shell-config",
// version 1), so files move freely between the iOS app and this one. Exports go to
// ~/Documents. See docs/CONFIG_FORMAT.md.
class ConfigTransfer : public QObject
{
    Q_OBJECT
public:
    ConfigTransfer(HostModel *hosts, SecretStore *secrets, QObject *parent = nullptr);

    // Writes the given hosts (all hosts when ids is empty) to a new file in ~/Documents.
    // Returns { ok, path, fileName, count } or { ok: false, error }.
    Q_INVOKABLE QVariantMap exportHosts(const QStringList &ids, bool includeSecrets);

    // Imports a file. A host whose id matches an existing one replaces it; others are
    // added. Returns { ok, count } or { ok: false, error }.
    Q_INVOKABLE QVariantMap importFile(const QString &path);

    // Same, from JSON text (used by importFile and the tests).
    QVariantMap importJson(const QByteArray &json);

private:
    HostModel *m_hosts;
    SecretStore *m_secrets;
};

#endif // CONFIGTRANSFER_H
