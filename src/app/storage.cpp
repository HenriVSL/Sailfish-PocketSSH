#include "app/storage.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

namespace Storage {

QString dataDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return dir;
}

bool writePrivateFile(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot write" << path << file.errorString();
        return false;
    }
    // Set before any data goes in; the rename on commit keeps the permissions.
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    file.write(data);
    if (!file.commit()) {
        qWarning() << "Cannot save" << path << file.errorString();
        return false;
    }
    return true;
}

}
