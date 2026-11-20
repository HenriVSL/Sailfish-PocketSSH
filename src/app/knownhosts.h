#ifndef KNOWNHOSTS_H
#define KNOWNHOSTS_H

#include <QMap>
#include <QObject>
#include <QVariantList>

// Trust-on-first-use store of server host keys, keyed by "host:port" (host lower-case),
// persisted as known_hosts.json in the app's data directory.
class KnownHosts : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    struct Entry
    {
        QString type;
        QString keyBase64;
        QString fingerprint;
    };

    enum Result { Trusted, Unknown, Changed };

    explicit KnownHosts(const QString &filePath, QObject *parent = nullptr);

    static QString key(const QString &host, int port);
    // previous receives the stored entry when the result is Changed.
    Result check(const QString &host, int port, const QString &type, const QString &keyBase64,
                 Entry *previous = nullptr) const;
    void trust(const QString &host, int port, const Entry &entry);

    int count() const { return m_entries.count(); }
    // For the "Trusted host keys" page: [{ key, type, fingerprint }], sorted by key.
    Q_INVOKABLE QVariantList entries() const;
    Q_INVOKABLE void remove(const QString &key);

signals:
    void changed();

private:
    void load();
    void persist() const;

    QString m_path;
    QMap<QString, Entry> m_entries;
};

#endif // KNOWNHOSTS_H
