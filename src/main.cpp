#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QScopedPointer>
#include <QtQml>

#include <sailfishapp.h>

#include "app/appsettings.h"
#include "app/configtransfer.h"
#include "app/hostmodel.h"
#include "app/knownhosts.h"
#include "app/secretstore.h"
#include "app/sessionmanager.h"
#include "app/storage.h"
#include "app/terminalsession.h"
#include "terminal/terminal.h"
#include "terminal/terminalitem.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    // Must match [X-Sailjail] in the .desktop file: together they decide the data path.
    app->setOrganizationName(QStringLiteral("io.github.henrivsl"));
    app->setApplicationName(QStringLiteral("harbour-pocketssh"));
    app->setApplicationVersion(QStringLiteral(APP_VERSION));

    qmlRegisterType<TerminalItem>("PocketSSH", 1, 0, "TerminalItem");
    qmlRegisterUncreatableType<Terminal>("PocketSSH", 1, 0, "Terminal", QStringLiteral("Owned by a session"));
    qmlRegisterUncreatableType<TerminalSession>("PocketSSH", 1, 0, "TerminalSession",
                                                QStringLiteral("Created by SessionManager"));

    const QString dir = Storage::dataDir();
    SecretStore secrets(dir + QStringLiteral("/secrets.json"));
    HostModel hosts(dir + QStringLiteral("/hosts.json"), &secrets);
    KnownHosts knownHosts(dir + QStringLiteral("/known_hosts.json"));
    ConfigTransfer transfer(&hosts, &secrets);
    AppSettings settings;
    SessionManager sessions(&hosts, &secrets, &knownHosts);

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    QQmlContext *ctx = view->rootContext();
    ctx->setContextProperty(QStringLiteral("hostModel"), &hosts);
    ctx->setContextProperty(QStringLiteral("knownHosts"), &knownHosts);
    ctx->setContextProperty(QStringLiteral("configTransfer"), &transfer);
    ctx->setContextProperty(QStringLiteral("appSettings"), &settings);
    ctx->setContextProperty(QStringLiteral("sessionManager"), &sessions);
    ctx->setContextProperty(QStringLiteral("appVersion"), app->applicationVersion());
    view->setSource(SailfishApp::pathToMainQml());
    view->show();

    const int result = app->exec();
    // Close the connections while the view, which shows their terminals, still exists.
    sessions.closeAll();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return result;
}
