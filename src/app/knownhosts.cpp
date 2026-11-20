#include "app/knownhosts.h"
#include "app/storage.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

KnownHosts::KnownHosts(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_path(filePath)
{
    load();
}

QString KnownHosts::key(const QString &host, int port)
{
    return host.toLower() + QLatin1Char(':') + QString::number(port);
}

KnownHosts::Result KnownHosts::check(const QString &host, int port, const QString &type,
                                     const QString &keyBase64, Entry *previous) const
{
    const auto it = m_entries.constFind(key(host, port));
    if (it == m_entries.constEnd())
        return Unknown;
    if (it->type == type && it->keyBase64 == keyBase64)
        return Trusted;
    if (previous)
        *previous = *it;
    return Changed;
}

void KnownHosts::trust(const QString &host, int port, const Entry &entry)
{
    m_entries.insert(key(host, port), entry);
    persist();
    emit changed();
}

QVariantList KnownHosts::entries() const
{
    QVariantList list;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        QVariantMap map;
        map.insert(QStringLiteral("key"), it.key());
        map.insert(QStringLiteral("type"), it->type);
        map.insert(QStringLiteral("fingerprint"), it->fingerprint);
        list.append(map);
    }
    return list;
}

void KnownHosts::remove(const QString &key)
{
    if (m_entries.remove(key)) {
        persist();
        emit changed();
    }
}

void KnownHosts::load()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject all = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.type = o.value(QStringLiteral("type")).toString();
        e.keyBase64 = o.value(QStringLiteral("keyBase64")).toString();
        e.fingerprint = o.value(QStringLiteral("fingerprint")).toString();
        if (!e.keyBase64.isEmpty())
            m_entries.insert(it.key(), e);
    }
}

void KnownHosts::persist() const
{
    QJsonObject all;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        QJsonObject o;
        o.insert(QStringLiteral("type"), it->type);
        o.insert(QStringLiteral("keyBase64"), it->keyBase64);
        o.insert(QStringLiteral("fingerprint"), it->fingerprint);
        all.insert(it.key(), o);
    }
    Storage::writePrivateFile(m_path, QJsonDocument(all).toJson());
}
