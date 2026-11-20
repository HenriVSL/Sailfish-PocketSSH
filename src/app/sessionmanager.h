#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include <QAbstractListModel>
#include <QList>

class HostModel;
class KnownHosts;
class SecretStore;
class TerminalSession;

// The open session tabs, as a list model for the tab strip and the host list's
// "Sessions" section, plus which one is current.
class SessionManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentChanged)
    Q_PROPERTY(TerminalSession *current READ current NOTIFY currentChanged)
    // Sessions that are connecting or connected (closed tabs don't count).
    Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)

public:
    enum Roles { SessionRole = Qt::UserRole + 1, TitleRole, AddressRole, StateRole };

    SessionManager(HostModel *hosts, SecretStore *secrets, KnownHosts *knownHosts, QObject *parent = nullptr);
    ~SessionManager() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_sessions.count(); }
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    TerminalSession *current() const;
    int activeCount() const;

    // Opens a new tab for the host, makes it current and starts connecting.
    Q_INVOKABLE TerminalSession *open(const QString &hostId);
    Q_INVOKABLE TerminalSession *get(int index) const;
    // Disconnects and removes the tab.
    Q_INVOKABLE void close(int index);
    Q_INVOKABLE void closeAll();
    Q_INVOKABLE void selectNext();

signals:
    void countChanged();
    void currentChanged();
    void activeCountChanged();

private:
    void sessionChanged();

    HostModel *m_hosts;
    SecretStore *m_secrets;
    KnownHosts *m_knownHosts;
    QList<TerminalSession *> m_sessions;
    int m_current = -1;
};

#endif // SESSIONMANAGER_H
