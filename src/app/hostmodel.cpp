#include "app/hostmodel.h"
#include "app/keyinfo.h"
#include "app/secretstore.h"
#include "app/storage.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

QString Host::address() const
{
    const QString base = username.isEmpty() ? hostname : username + QLatin1Char('@') + hostname;
    return port == 22 ? base : base + QLatin1Char(':') + QString::number(port);
}

bool Host::isValid() const
{
    return !hostname.trimmed().isEmpty() && port >= 1 && port <= 65535;
}

QJsonObject Host::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("hostname"), hostname);
    o.insert(QStringLiteral("port"), port);
    o.insert(QStringLiteral("username"), username);
    o.insert(QStringLiteral("authMethod"), authMethod);
    return o;
}

Host Host::fromJson(const QJsonObject &o)
{
    Host h;
    // Pocket Shell decodes ids as UUIDs, so anything else gets a fresh one.
    const QUuid uuid(o.value(QStringLiteral("id")).toString());
    h.id = uuid.isNull() ? newId() : uuid.toString().mid(1, 36).toUpper();
    h.name = o.value(QStringLiteral("name")).toString();
    h.hostname = o.value(QStringLiteral("hostname")).toString().trimmed();
    h.port = o.value(QStringLiteral("port")).toInt(22);
    h.username = o.value(QStringLiteral("username")).toString().trimmed();
    const QString method = o.value(QStringLiteral("authMethod")).toString();
    h.authMethod = method == QLatin1String("privateKey") ? method : QStringLiteral("password");
    return h;
}

QString Host::newId()
{
    // "{xxxxxxxx-...}" -> "XXXXXXXX-...", the form Swift's UUID uses.
    return QUuid::createUuid().toString().mid(1, 36).toUpper();
}

HostModel::HostModel(const QString &filePath, SecretStore *secrets, QObject *parent)
    : QAbstractListModel(parent)
    , m_path(filePath)
    , m_secrets(secrets)
{
    load();
}

int HostModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_hosts.count();
}

QVariant HostModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_hosts.count())
        return QVariant();
    const Host &h = m_hosts.at(index.row());
    switch (role) {
    case HostIdRole: return h.id;
    case NameRole: return h.name;
    case HostnameRole: return h.hostname;
    case PortRole: return h.port;
    case UsernameRole: return h.username;
    case AuthMethodRole: return h.authMethod;
    case DisplayNameRole: return h.displayName();
    case AddressRole: return h.address();
    case HasPasswordRole: return !m_secrets->secrets(h.id).password.isEmpty();
    case HasPrivateKeyRole: return !m_secrets->secrets(h.id).privateKey.isEmpty();
    }
    return QVariant();
}

QHash<int, QByteArray> HostModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[HostIdRole] = "hostId";
    roles[NameRole] = "name";
    roles[HostnameRole] = "hostname";
    roles[PortRole] = "port";
    roles[UsernameRole] = "username";
    roles[AuthMethodRole] = "authMethod";
    roles[DisplayNameRole] = "displayName";
    roles[AddressRole] = "address";
    roles[HasPasswordRole] = "hasPassword";
    roles[HasPrivateKeyRole] = "hasPrivateKey";
    return roles;
}

Host HostModel::host(const QString &id) const
{
    const int i = indexOf(id);
    return i >= 0 ? m_hosts.at(i) : Host();
}

void HostModel::upsert(const Host &host)
{
    const int i = indexOf(host.id);
    if (i >= 0) {
        m_hosts[i] = host;
        emit dataChanged(index(i), index(i));
    } else {
        beginInsertRows(QModelIndex(), m_hosts.count(), m_hosts.count());
        m_hosts.append(host);
        endInsertRows();
        emit countChanged();
    }
    persist();
}

QVariantMap HostModel::get(const QString &id) const
{
    QVariantMap map;
    const int i = indexOf(id);
    if (i < 0)
        return map;
    const Host &h = m_hosts.at(i);
    map = h.toJson().toVariantMap();
    map.insert(QStringLiteral("displayName"), h.displayName());
    map.insert(QStringLiteral("address"), h.address());
    const HostSecrets s = m_secrets->secrets(h.id);
    map.insert(QStringLiteral("hasPassword"), !s.password.isEmpty());
    map.insert(QStringLiteral("hasPrivateKey"), !s.privateKey.isEmpty());
    map.insert(QStringLiteral("hasPassphrase"), !s.passphrase.isEmpty());
    if (!s.privateKey.isEmpty()) {
        const KeyInfo info = KeyInfo::inspect(s.privateKey);
        map.insert(QStringLiteral("keyType"), info.type);
        map.insert(QStringLiteral("keyEncrypted"), info.encrypted);
    }
    return map;
}

QString HostModel::save(const QVariantMap &fields)
{
    Host h = Host::fromJson(QJsonObject::fromVariantMap(fields));
    upsert(h);
    return h.id;
}

void HostModel::remove(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    beginRemoveRows(QModelIndex(), i, i);
    m_hosts.removeAt(i);
    endRemoveRows();
    m_secrets->remove(id);
    persist();
    emit countChanged();
}

QString HostModel::duplicate(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0)
        return QString();
    Host copy = m_hosts.at(i);
    copy.id = Host::newId();
    copy.name = tr("%1 copy").arg(m_hosts.at(i).displayName());
    m_secrets->setSecrets(copy.id, m_secrets->secrets(id));
    beginInsertRows(QModelIndex(), i + 1, i + 1);
    m_hosts.insert(i + 1, copy);
    endInsertRows();
    persist();
    emit countChanged();
    return copy.id;
}

void HostModel::move(int from, int to)
{
    if (from < 0 || from >= m_hosts.count() || to < 0 || to >= m_hosts.count() || from == to)
        return;
    // beginMoveRows wants the row the item lands before, in pre-move numbering.
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
    m_hosts.move(from, to);
    endMoveRows();
    persist();
}

void HostModel::setPassword(const QString &id, const QString &password)
{
    m_secrets->setPassword(id, password);
    refreshSecretRoles(id);
}

void HostModel::setPrivateKey(const QString &id, const QString &privateKey, const QString &passphrase)
{
    m_secrets->setPrivateKey(id, privateKey.trimmed().isEmpty() ? QString() : privateKey.trimmed() + QLatin1Char('\n'),
                             passphrase);
    refreshSecretRoles(id);
}

void HostModel::setPassphrase(const QString &id, const QString &passphrase)
{
    m_secrets->setPassphrase(id, passphrase);
}

QVariantMap HostModel::readKeyFile(const QString &path) const
{
    QVariantMap result;
    QFile file(path);
    if (QFileInfo(path).size() > 64 * 1024) {
        result.insert(QStringLiteral("error"), tr("The file is too large to be a private key."));
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        result.insert(QStringLiteral("error"), tr("Cannot read the file: %1").arg(file.errorString()));
        return result;
    }
    const QString key = QString::fromUtf8(file.readAll());
    result = inspectKey(key);
    if (result.value(QStringLiteral("ok")).toBool())
        result.insert(QStringLiteral("key"), key);
    return result;
}

QVariantMap HostModel::inspectKey(const QString &key) const
{
    QVariantMap result;
    const KeyInfo info = KeyInfo::inspect(key);
    result.insert(QStringLiteral("ok"), info.valid);
    if (!info.valid) {
        result.insert(QStringLiteral("error"), key.contains(QLatin1String("PUBLIC KEY")) || key.startsWith(QLatin1String("ssh-"))
                      ? tr("This is a public key. Choose the private key (the file without .pub).")
                      : tr("This doesn't look like an OpenSSH or PEM private key."));
        return result;
    }
    result.insert(QStringLiteral("type"), info.type);
    result.insert(QStringLiteral("encrypted"), info.encrypted);
    return result;
}

int HostModel::indexOf(const QString &id) const
{
    for (int i = 0; i < m_hosts.count(); ++i) {
        if (m_hosts.at(i).id == id)
            return i;
    }
    return -1;
}

void HostModel::load()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue &v : array) {
        const Host h = Host::fromJson(v.toObject());
        if (h.isValid() && indexOf(h.id) < 0)
            m_hosts.append(h);
    }
}

void HostModel::persist() const
{
    QJsonArray array;
    for (const Host &h : m_hosts)
        array.append(h.toJson());
    Storage::writePrivateFile(m_path, QJsonDocument(array).toJson());
}

void HostModel::refreshSecretRoles(const QString &id)
{
    const int i = indexOf(id);
    if (i >= 0)
        emit dataChanged(index(i), index(i), QVector<int>() << HasPasswordRole << HasPrivateKeyRole);
}
