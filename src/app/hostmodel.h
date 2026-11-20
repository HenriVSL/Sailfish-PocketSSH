#ifndef HOSTMODEL_H
#define HOSTMODEL_H

#include <QAbstractListModel>
#include <QJsonObject>
#include <QList>
#include <QVariantMap>

class SecretStore;

// A saved host. Secrets are kept in SecretStore, keyed by id, never here.
struct Host
{
    QString id;   // upper-case UUID without braces (same form as Pocket Shell on iOS)
    QString name;
    QString hostname;
    int port = 22;
    QString username;
    QString authMethod = QStringLiteral("password"); // "password" or "privateKey"

    QString displayName() const { return name.isEmpty() ? hostname : name; }
    QString address() const;
    bool isValid() const;

    QJsonObject toJson() const;
    static Host fromJson(const QJsonObject &o);
    static QString newId();
};

// The host list, persisted as hosts.json in the app's data directory.
class HostModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        HostIdRole = Qt::UserRole + 1,
        NameRole,
        HostnameRole,
        PortRole,
        UsernameRole,
        AuthMethodRole,
        DisplayNameRole,
        AddressRole,
        HasPasswordRole,
        HasPrivateKeyRole,
    };

    HostModel(const QString &filePath, SecretStore *secrets, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_hosts.count(); }

    const QList<Host> &hosts() const { return m_hosts; }
    bool contains(const QString &id) const { return indexOf(id) >= 0; }
    Host host(const QString &id) const;
    // Inserts a new host or replaces the one with the same id.
    void upsert(const Host &host);

    // For QML. A host as a map with the fields of Host plus displayName, address,
    // hasPassword, hasPrivateKey and keyType/keyEncrypted (when a key is saved).
    Q_INVOKABLE QVariantMap get(const QString &id) const;
    // Saves the non-secret fields (id, name, hostname, port, username, authMethod).
    // An empty or unknown id adds a new host. Returns the host's id.
    Q_INVOKABLE QString save(const QVariantMap &fields);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE QString duplicate(const QString &id);
    Q_INVOKABLE void move(int from, int to);

    Q_INVOKABLE void setPassword(const QString &id, const QString &password);
    Q_INVOKABLE void setPrivateKey(const QString &id, const QString &privateKey, const QString &passphrase);
    Q_INVOKABLE void setPassphrase(const QString &id, const QString &passphrase);
    // Reads a private key file chosen in the file picker.
    // Returns { ok, key, type, encrypted, error }.
    Q_INVOKABLE QVariantMap readKeyFile(const QString &path) const;
    // Checks pasted key text. Returns { ok, type, encrypted, error }.
    Q_INVOKABLE QVariantMap inspectKey(const QString &key) const;

signals:
    void countChanged();

private:
    int indexOf(const QString &id) const;
    void load();
    void persist() const;
    void refreshSecretRoles(const QString &id);

    QString m_path;
    SecretStore *m_secrets;
    QList<Host> m_hosts;
};

#endif // HOSTMODEL_H
