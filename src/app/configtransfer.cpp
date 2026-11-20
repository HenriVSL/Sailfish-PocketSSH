#include "app/configtransfer.h"
#include "app/hostmodel.h"
#include "app/secretstore.h"
#include "app/storage.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegExp>
#include <QStandardPaths>

namespace {

const char *const FormatId = "pocket-shell-config";

QString sanitize(const QString &name)
{
    QString cleaned = name;
    cleaned.replace(QRegExp(QStringLiteral("[/\\\\:?%*|\"<>\\s]+")), QStringLiteral("-"));
    return cleaned;
}

// ~/Documents/<base>.json, or <base>-2.json, -3, ... if taken.
QString uniquePath(const QString &dir, const QString &base)
{
    QString path = dir + QLatin1Char('/') + base + QStringLiteral(".json");
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = dir + QLatin1Char('/') + base + QLatin1Char('-') + QString::number(n) + QStringLiteral(".json");
    return path;
}

QVariantMap failure(const QString &error)
{
    QVariantMap result;
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("error"), error);
    return result;
}

}

ConfigTransfer::ConfigTransfer(HostModel *hosts, SecretStore *secrets, QObject *parent)
    : QObject(parent)
    , m_hosts(hosts)
    , m_secrets(secrets)
{
}

QVariantMap ConfigTransfer::exportHosts(const QStringList &ids, bool includeSecrets)
{
    QList<Host> hosts;
    for (const Host &h : m_hosts->hosts()) {
        if (ids.isEmpty() || ids.contains(h.id))
            hosts.append(h);
    }
    if (hosts.isEmpty())
        return failure(tr("There are no hosts to export."));

    QJsonArray entries;
    for (const Host &h : hosts) {
        QJsonObject o = h.toJson();
        if (includeSecrets) {
            const HostSecrets s = m_secrets->secrets(h.id);
            if (!s.password.isEmpty())
                o.insert(QStringLiteral("password"), s.password);
            if (!s.privateKey.isEmpty())
                o.insert(QStringLiteral("privateKey"), s.privateKey);
            if (!s.passphrase.isEmpty())
                o.insert(QStringLiteral("passphrase"), s.passphrase);
        }
        entries.append(o);
    }
    QJsonObject file;
    file.insert(QStringLiteral("format"), QString::fromLatin1(FormatId));
    file.insert(QStringLiteral("version"), 1);
    file.insert(QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    file.insert(QStringLiteral("includesSecrets"), includeSecrets);
    file.insert(QStringLiteral("hosts"), entries);

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QDir().mkpath(dir);
    QString base = ids.size() == 1 ? QStringLiteral("pocketssh-") + sanitize(hosts.first().displayName())
                                   : QStringLiteral("pocketssh-hosts");
    if (includeSecrets)
        base += QStringLiteral("-with-secrets");
    const QString path = uniquePath(dir, base);

    const QByteArray json = QJsonDocument(file).toJson();
    bool written;
    if (includeSecrets) {
        written = Storage::writePrivateFile(path, json);
    } else {
        QFile out(path);
        written = out.open(QIODevice::WriteOnly) && out.write(json) == json.size();
    }
    if (!written)
        return failure(tr("Could not write %1").arg(path));

    QVariantMap result;
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("fileName"), QFileInfo(path).fileName());
    result.insert(QStringLiteral("count"), hosts.size());
    return result;
}

QVariantMap ConfigTransfer::importFile(const QString &path)
{
    QFile file(path);
    if (QFileInfo(path).size() > 4 * 1024 * 1024)
        return failure(tr("This file is not a Pocket Shell configuration."));
    if (!file.open(QIODevice::ReadOnly))
        return failure(tr("Cannot read the file: %1").arg(file.errorString()));
    return importJson(file.readAll());
}

QVariantMap ConfigTransfer::importJson(const QByteArray &json)
{
    const QJsonObject file = QJsonDocument::fromJson(json).object();
    if (file.value(QStringLiteral("format")).toString() != QLatin1String(FormatId)
            || !file.value(QStringLiteral("hosts")).isArray())
        return failure(tr("This file is not a Pocket Shell configuration."));
    if (file.value(QStringLiteral("version")).toInt(1) > 1)
        return failure(tr("This file was made by a newer version of the app. Update PocketSSH to import it."));

    int count = 0;
    for (const QJsonValue &value : file.value(QStringLiteral("hosts")).toArray()) {
        const QJsonObject o = value.toObject();
        HostSecrets imported;
        imported.password = o.value(QStringLiteral("password")).toString();
        imported.privateKey = o.value(QStringLiteral("privateKey")).toString();
        imported.passphrase = o.value(QStringLiteral("passphrase")).toString();

        Host host = Host::fromJson(o);
        if (!o.contains(QStringLiteral("authMethod")))
            host.authMethod = imported.privateKey.isEmpty() ? QStringLiteral("password") : QStringLiteral("privateKey");
        if (!host.isValid())
            continue;

        // Secrets in the file replace the stored ones field by field; fields the file
        // doesn't have are left as they are.
        if (!imported.isEmpty()) {
            HostSecrets s = m_secrets->secrets(host.id);
            if (!imported.password.isEmpty())
                s.password = imported.password;
            if (!imported.privateKey.isEmpty()) {
                s.privateKey = imported.privateKey;
                s.passphrase = imported.passphrase;
            } else if (!imported.passphrase.isEmpty()) {
                s.passphrase = imported.passphrase;
            }
            m_secrets->setSecrets(host.id, s);
        }
        m_hosts->upsert(host);
        ++count;
    }

    QVariantMap result;
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("count"), count);
    return result;
}
