#include "app/sessionmanager.h"
#include "app/hostmodel.h"
#include "app/terminalsession.h"

#include <QQmlEngine>

SessionManager::SessionManager(HostModel *hosts, SecretStore *secrets, KnownHosts *knownHosts, QObject *parent)
    : QAbstractListModel(parent)
    , m_hosts(hosts)
    , m_secrets(secrets)
    , m_knownHosts(knownHosts)
{
}

SessionManager::~SessionManager()
{
    qDeleteAll(m_sessions);
}

int SessionManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_sessions.count();
}

QVariant SessionManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_sessions.count())
        return QVariant();
    TerminalSession *s = m_sessions.at(index.row());
    switch (role) {
    case SessionRole: return QVariant::fromValue(s);
    case TitleRole: return s->title();
    case AddressRole: return s->address();
    case StateRole: return int(s->state());
    }
    return QVariant();
}

QHash<int, QByteArray> SessionManager::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[SessionRole] = "session";
    roles[TitleRole] = "title";
    roles[AddressRole] = "address";
    roles[StateRole] = "sessionState";
    return roles;
}

void SessionManager::setCurrentIndex(int index)
{
    if (index < -1 || index >= m_sessions.count() || index == m_current)
        return;
    m_current = index;
    emit currentChanged();
}

TerminalSession *SessionManager::current() const
{
    return m_current >= 0 && m_current < m_sessions.count() ? m_sessions.at(m_current) : nullptr;
}

int SessionManager::activeCount() const
{
    int n = 0;
    for (TerminalSession *s : m_sessions) {
        if (s->state() != TerminalSession::Closed)
            ++n;
    }
    return n;
}

TerminalSession *SessionManager::open(const QString &hostId)
{
    if (!m_hosts->contains(hostId))
        return nullptr;
    const int row = m_sessions.count();
    beginInsertRows(QModelIndex(), row, row);
    TerminalSession *s = new TerminalSession(m_hosts->host(hostId), m_hosts, m_secrets, m_knownHosts);
    // Returned to QML from Q_INVOKABLEs: without this the JS engine would own (and
    // garbage-collect) the parentless session.
    QQmlEngine::setObjectOwnership(s, QQmlEngine::CppOwnership);
    m_sessions.append(s);
    endInsertRows();
    connect(s, &TerminalSession::titleChanged, this, &SessionManager::sessionChanged);
    connect(s, &TerminalSession::addressChanged, this, &SessionManager::sessionChanged);
    connect(s, &TerminalSession::stateChanged, this, &SessionManager::sessionChanged);
    connect(s, &TerminalSession::stateChanged, this, &SessionManager::activeCountChanged);
    emit countChanged();
    emit activeCountChanged();
    m_current = row;
    emit currentChanged();
    return s;
}

TerminalSession *SessionManager::get(int index) const
{
    return index >= 0 && index < m_sessions.count() ? m_sessions.at(index) : nullptr;
}

void SessionManager::close(int index)
{
    if (index < 0 || index >= m_sessions.count())
        return;
    beginRemoveRows(QModelIndex(), index, index);
    TerminalSession *s = m_sessions.takeAt(index);
    endRemoveRows();
    s->QObject::disconnect(this);
    s->disconnectFromHost();
    // Views may still hold the session for this event-loop pass.
    s->deleteLater();

    const int old = m_current;
    if (m_sessions.isEmpty())
        m_current = -1;
    else if (index < m_current || m_current >= m_sessions.count())
        m_current = qMax(0, m_current - 1);
    emit countChanged();
    emit activeCountChanged();
    if (old != m_current || index == old)
        emit currentChanged();
}

void SessionManager::closeAll()
{
    while (!m_sessions.isEmpty())
        close(m_sessions.count() - 1);
}

void SessionManager::selectNext()
{
    if (!m_sessions.isEmpty())
        setCurrentIndex((m_current + 1) % m_sessions.count());
}

void SessionManager::sessionChanged()
{
    const int row = m_sessions.indexOf(static_cast<TerminalSession *>(sender()));
    if (row >= 0)
        emit dataChanged(index(row), index(row));
}
