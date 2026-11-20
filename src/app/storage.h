#ifndef STORAGE_H
#define STORAGE_H

#include <QByteArray>
#include <QString>

// Where the app keeps its files, and how it writes them.
namespace Storage {

// The app's private data directory (created 0700 on first use). Under Sailjail this is
// ~/.local/share/io.github.henrivsl/harbour-pocketssh.
QString dataDir();

// Writes atomically (temp file + rename) with owner-only permissions (0600).
bool writePrivateFile(const QString &path, const QByteArray &data);

}

#endif // STORAGE_H
