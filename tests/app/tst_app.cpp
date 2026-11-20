// Host unit tests for the app layer: host list, secrets, known hosts, key inspection
// and the Pocket Shell export/import format. Runs with HOME pointed at a temp dir.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

#include "app/configtransfer.h"
#include "app/hostmodel.h"
#include "app/keyinfo.h"
#include "app/knownhosts.h"
#include "app/secretstore.h"
#include "app/storage.h"

static QTemporaryDir *g_home = nullptr;

class TestApp : public QObject
{
    Q_OBJECT

private:
    QString dataPath(const QString &name) const { return Storage::dataDir() + QLatin1Char('/') + name; }

    static QString makeKey(const QString &type, const QString &passphrase, bool pem = false)
    {
        const QString path = g_home->path() + QStringLiteral("/key-") + type + (pem ? "-pem" : "")
                + (passphrase.isEmpty() ? "" : "-enc");
        QFile::remove(path);
        QFile::remove(path + ".pub");
        QStringList args { "-q", "-t", type, "-N", passphrase, "-f", path };
        if (pem)
            args << "-m" << "PEM";
        if (QProcess::execute(QStringLiteral("ssh-keygen"), args) != 0)
            return QString();
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        return QString::fromUtf8(f.readAll());
    }

    void clearData()
    {
        QFile::remove(dataPath("hosts.json"));
        QFile::remove(dataPath("secrets.json"));
        QFile::remove(dataPath("known_hosts.json"));
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).removeRecursively();
    }

private slots:
    void init() { clearData(); }

    void keyInfo_data()
    {
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("passphrase");
        QTest::addColumn<bool>("pem");
        QTest::addColumn<QString>("expectedType");
        QTest::newRow("ed25519") << "ed25519" << "" << false << "Ed25519";
        QTest::newRow("ed25519 encrypted") << "ed25519" << "secret" << false << "Ed25519";
        QTest::newRow("ecdsa") << "ecdsa" << "" << false << "ECDSA";
        QTest::newRow("rsa") << "rsa" << "" << false << "RSA";
        QTest::newRow("rsa pem") << "rsa" << "" << true << "RSA";
        QTest::newRow("rsa pem encrypted") << "rsa" << "secret" << true << "RSA";
        QTest::newRow("ecdsa pem") << "ecdsa" << "" << true << "ECDSA";
    }

    void keyInfo()
    {
        QFETCH(QString, type);
        QFETCH(QString, passphrase);
        QFETCH(bool, pem);
        QFETCH(QString, expectedType);
        const QString key = makeKey(type, passphrase, pem);
        QVERIFY(!key.isEmpty());
        const KeyInfo info = KeyInfo::inspect(key);
        QVERIFY(info.valid);
        QCOMPARE(info.type, expectedType);
        QCOMPARE(info.encrypted, !passphrase.isEmpty());
    }

    void keyInfoRejectsJunk()
    {
        QVERIFY(!KeyInfo::inspect(QString()).valid);
        QVERIFY(!KeyInfo::inspect(QStringLiteral("ssh-ed25519 AAAAC3Nza... me@host")).valid);
        QVERIFY(!KeyInfo::inspect(QStringLiteral("-----BEGIN PUBLIC KEY-----\nAAAA\n-----END PUBLIC KEY-----")).valid);
        QVERIFY(!KeyInfo::inspect(QStringLiteral("-----BEGIN OPENSSH PRIVATE KEY-----\nbm9wZQ==\n-----END OPENSSH PRIVATE KEY-----")).valid);
    }

    void hostsPersistAndSecretsStaySeparate()
    {
        QString id;
        {
            SecretStore secrets(dataPath("secrets.json"));
            HostModel model(dataPath("hosts.json"), &secrets);
            QVariantMap fields;
            fields["name"] = "pi";
            fields["hostname"] = " 192.168.1.20 ";
            fields["port"] = 2222;
            fields["username"] = "pi";
            fields["authMethod"] = "password";
            id = model.save(fields);
            QCOMPARE(id.size(), 36);
            QCOMPARE(id, id.toUpper());
            model.setPassword(id, "hunter2");
            QCOMPARE(model.count(), 1);
            QCOMPARE(model.get(id).value("hasPassword").toBool(), true);
            QCOMPARE(model.get(id).value("address").toString(), QStringLiteral("pi@192.168.1.20:2222"));
        }
        SecretStore secrets(dataPath("secrets.json"));
        HostModel model(dataPath("hosts.json"), &secrets);
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.host(id).hostname, QStringLiteral("192.168.1.20"));
        QCOMPARE(secrets.secrets(id).password, QStringLiteral("hunter2"));

        QFile hostsFile(dataPath("hosts.json"));
        QVERIFY(hostsFile.open(QIODevice::ReadOnly));
        QVERIFY(!hostsFile.readAll().contains("hunter2"));
        QCOMPARE(QFileInfo(dataPath("secrets.json")).permissions() & 0x0FFF,
                 QFile::ReadUser | QFile::WriteUser); // user rw, nothing for group or others
    }

    void duplicateMoveRemove()
    {
        SecretStore secrets(dataPath("secrets.json"));
        HostModel model(dataPath("hosts.json"), &secrets);
        QVariantMap a; a["hostname"] = "a.example"; a["username"] = "me";
        QVariantMap b; b["hostname"] = "b.example";
        const QString idA = model.save(a);
        const QString idB = model.save(b);
        model.setPrivateKey(idA, makeKey("ed25519", ""), "");
        const QString copy = model.duplicate(idA);
        QCOMPARE(model.count(), 3);
        QCOMPARE(model.hosts().at(1).id, copy);
        QCOMPARE(model.host(copy).name, QStringLiteral("a.example copy"));
        QCOMPARE(secrets.secrets(copy).privateKey, secrets.secrets(idA).privateKey);
        QVERIFY(model.get(copy).value("keyType").toString() == QLatin1String("Ed25519"));

        model.move(2, 0);
        QCOMPARE(model.hosts().at(0).id, idB);
        model.move(0, 2);
        QCOMPARE(model.hosts().at(2).id, idB);

        model.remove(copy);
        QCOMPARE(model.count(), 2);
        QVERIFY(secrets.secrets(copy).isEmpty());
        QVERIFY(!secrets.secrets(idA).isEmpty());
    }

    void knownHosts()
    {
        KnownHosts kh(dataPath("known_hosts.json"));
        QCOMPARE(kh.check("Example.COM", 22, "ssh-ed25519", "AAAA"), KnownHosts::Unknown);
        KnownHosts::Entry e { "ssh-ed25519", "AAAA", "SHA256:abc" };
        kh.trust("Example.COM", 22, e);
        QCOMPARE(kh.check("example.com", 22, "ssh-ed25519", "AAAA"), KnownHosts::Trusted);
        QCOMPARE(kh.check("example.com", 2222, "ssh-ed25519", "AAAA"), KnownHosts::Unknown);
        KnownHosts::Entry previous;
        QCOMPARE(kh.check("example.com", 22, "ssh-ed25519", "BBBB", &previous), KnownHosts::Changed);
        QCOMPARE(previous.fingerprint, QStringLiteral("SHA256:abc"));

        KnownHosts reloaded(dataPath("known_hosts.json"));
        QCOMPARE(reloaded.count(), 1);
        QCOMPARE(reloaded.entries().first().toMap().value("key").toString(), QStringLiteral("example.com:22"));
        reloaded.remove("example.com:22");
        QCOMPARE(reloaded.count(), 0);
    }

    void exportWithoutAndWithSecrets()
    {
        SecretStore secrets(dataPath("secrets.json"));
        HostModel model(dataPath("hosts.json"), &secrets);
        QVariantMap a; a["name"] = "web/server"; a["hostname"] = "example.com"; a["username"] = "deploy";
        const QString id = model.save(a);
        model.setPassword(id, "pw1");
        ConfigTransfer transfer(&model, &secrets);

        QVariantMap plain = transfer.exportHosts(QStringList(), false);
        QVERIFY2(plain.value("ok").toBool(), qPrintable(plain.value("error").toString()));
        QCOMPARE(plain.value("fileName").toString(), QStringLiteral("pocketssh-hosts.json"));
        QFile f(plain.value("path").toString());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(doc.value("format").toString(), QStringLiteral("pocket-shell-config"));
        QCOMPARE(doc.value("version").toInt(), 1);
        QCOMPARE(doc.value("includesSecrets").toBool(), false);
        QVERIFY(doc.value("exportedAt").toString().endsWith('Z'));
        const QJsonObject h = doc.value("hosts").toArray().first().toObject();
        QCOMPARE(h.value("id").toString(), id);
        QCOMPARE(h.value("port").toInt(), 22);
        QCOMPARE(h.value("authMethod").toString(), QStringLiteral("password"));
        QVERIFY(!h.contains("password"));

        QVariantMap again = transfer.exportHosts(QStringList(), false);
        QCOMPARE(again.value("fileName").toString(), QStringLiteral("pocketssh-hosts-2.json"));

        QVariantMap single = transfer.exportHosts(QStringList() << id, true);
        QCOMPARE(single.value("fileName").toString(), QStringLiteral("pocketssh-web-server-with-secrets.json"));
        QCOMPARE(QFileInfo(single.value("path").toString()).permissions() & 0x0FFF,
                 QFile::ReadUser | QFile::WriteUser); // user rw, nothing for group or others
        QFile s(single.value("path").toString());
        QVERIFY(s.open(QIODevice::ReadOnly));
        QVERIFY(s.readAll().contains("\"password\": \"pw1\""));
    }

    void importHandWrittenPocketShellFile()
    {
        // The example from Pocket Shell's docs/CONFIG_FORMAT.md.
        const QByteArray json = R"({
          "format": "pocket-shell-config", "version": 1,
          "exportedAt": "2026-01-01T00:00:00Z", "includesSecrets": false,
          "hosts": [
            { "name": "pi", "hostname": "192.168.1.20", "username": "pi" },
            { "name": "vps", "hostname": "vps.example.com", "port": 2222, "username": "me", "authMethod": "privateKey" },
            { "name": "broken", "hostname": "", "username": "x" }
          ] })";
        SecretStore secrets(dataPath("secrets.json"));
        HostModel model(dataPath("hosts.json"), &secrets);
        ConfigTransfer transfer(&model, &secrets);
        const QVariantMap r = transfer.importJson(json);
        QVERIFY(r.value("ok").toBool());
        QCOMPARE(r.value("count").toInt(), 2);
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.hosts().at(0).port, 22);
        QCOMPARE(model.hosts().at(0).authMethod, QStringLiteral("password"));
        QCOMPARE(model.hosts().at(1).port, 2222);
        QCOMPARE(model.hosts().at(1).authMethod, QStringLiteral("privateKey"));
    }

    void importReplacesByIdAndMergesSecrets()
    {
        SecretStore secrets(dataPath("secrets.json"));
        HostModel model(dataPath("hosts.json"), &secrets);
        QVariantMap a; a["hostname"] = "old.example"; a["username"] = "me";
        const QString id = model.save(a);
        model.setPassword(id, "keepme");
        ConfigTransfer transfer(&model, &secrets);

        const QString key = makeKey("ed25519", "");
        QJsonObject h { { "id", id }, { "name", "new" }, { "hostname", "new.example" }, { "username", "me" },
                        { "privateKey", key } };
        QJsonObject file { { "format", "pocket-shell-config" }, { "version", 1 }, { "includesSecrets", true },
                           { "exportedAt", "2026-01-01T00:00:00Z" }, { "hosts", QJsonArray { h } } };
        const QVariantMap r = transfer.importJson(QJsonDocument(file).toJson());
        QVERIFY(r.value("ok").toBool());
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.host(id).hostname, QStringLiteral("new.example"));
        QCOMPARE(model.host(id).authMethod, QStringLiteral("privateKey")); // no authMethod + key present
        QCOMPARE(secrets.secrets(id).password, QStringLiteral("keepme"));
        QCOMPARE(secrets.secrets(id).privateKey, key);
    }

    void importRejectsOtherFiles()
    {
        SecretStore secrets(dataPath("secrets.json"));
        HostModel model(dataPath("hosts.json"), &secrets);
        ConfigTransfer transfer(&model, &secrets);
        QVERIFY(!transfer.importJson("{\"format\":\"something-else\",\"hosts\":[]}").value("ok").toBool());
        QVERIFY(!transfer.importJson("not json").value("ok").toBool());
        QVERIFY(!transfer.importFile(g_home->path() + "/missing.json").value("ok").toBool());
    }

    void importNormalizesIdsAndChecksVersion()
    {
        SecretStore secrets(dataPath("secrets.json"));
        HostModel model(dataPath("hosts.json"), &secrets);
        QVariantMap a; a["hostname"] = "x.example";
        const QString id = model.save(a);
        ConfigTransfer transfer(&model, &secrets);
        // A lower-case or braced form of an existing id replaces that host; junk gets a new UUID.
        QJsonArray hosts { QJsonObject { { "id", id.toLower() }, { "hostname", "y.example" }, { "name", "" }, { "username", "" } },
                           QJsonObject { { "id", "{" + id + "}" }, { "hostname", "z.example" }, { "name", "" }, { "username", "" } },
                           QJsonObject { { "id", "pi" }, { "hostname", "pi.local" }, { "name", "" }, { "username", "" } } };
        QJsonObject file { { "format", "pocket-shell-config" }, { "version", 1 }, { "hosts", hosts } };
        QCOMPARE(transfer.importJson(QJsonDocument(file).toJson()).value("count").toInt(), 3);
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.host(id).hostname, QStringLiteral("z.example"));
        const QString newId = model.hosts().at(1).id;
        QCOMPARE(newId.size(), 36);
        QVERIFY(!QUuid(newId).isNull());

        file["version"] = 2;
        QVERIFY(!transfer.importJson(QJsonDocument(file).toJson()).value("ok").toBool());
    }

    void roundTripExportImport()
    {
        QString exported;
        QString id;
        {
            SecretStore secrets(dataPath("secrets.json"));
            HostModel model(dataPath("hosts.json"), &secrets);
            QVariantMap a; a["hostname"] = "rt.example"; a["username"] = "u"; a["authMethod"] = "privateKey";
            id = model.save(a);
            model.setPrivateKey(id, makeKey("ecdsa", "phrase"), "phrase");
            ConfigTransfer transfer(&model, &secrets);
            exported = transfer.exportHosts(QStringList(), true).value("path").toString();
        }
        clearDataKeepDocuments();
        SecretStore secrets(dataPath("secrets.json"));
        HostModel model(dataPath("hosts.json"), &secrets);
        ConfigTransfer transfer(&model, &secrets);
        QCOMPARE(transfer.importFile(exported).value("count").toInt(), 1);
        QCOMPARE(model.hosts().first().id, id);
        QCOMPARE(secrets.secrets(id).passphrase, QStringLiteral("phrase"));
        QVERIFY(KeyInfo::inspect(secrets.secrets(id).privateKey).encrypted);
    }

private:
    void clearDataKeepDocuments()
    {
        QFile::remove(dataPath("hosts.json"));
        QFile::remove(dataPath("secrets.json"));
    }
};

int main(int argc, char *argv[])
{
    QTemporaryDir home;
    g_home = &home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/.local/share").toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
    QCoreApplication app(argc, argv);
    app.setOrganizationName("io.github.henrivsl");
    app.setApplicationName("harbour-pocketssh");
    TestApp test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_app.moc"
