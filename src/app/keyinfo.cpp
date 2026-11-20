#include "app/keyinfo.h"

#include <QByteArray>
#include <QStringList>

namespace {

// Reads an SSH wire-format string (uint32 length + bytes) at pos.
bool readString(const QByteArray &data, int &pos, QByteArray &out)
{
    if (pos + 4 > data.size())
        return false;
    const quint32 len = (quint32(quint8(data[pos])) << 24) | (quint32(quint8(data[pos + 1])) << 16)
                      | (quint32(quint8(data[pos + 2])) << 8) | quint32(quint8(data[pos + 3]));
    pos += 4;
    if (len > quint32(data.size() - pos))
        return false;
    out = data.mid(pos, int(len));
    pos += int(len);
    return true;
}

QString typeFromName(const QByteArray &name)
{
    if (name == "ssh-ed25519")
        return QStringLiteral("Ed25519");
    if (name == "ssh-rsa")
        return QStringLiteral("RSA");
    if (name.startsWith("ecdsa-sha2-"))
        return QStringLiteral("ECDSA");
    if (name == "ssh-dss")
        return QStringLiteral("DSA");
    return QString::fromLatin1(name);
}

}

KeyInfo KeyInfo::inspect(const QString &keyText)
{
    KeyInfo info;
    const QString text = keyText.trimmed();
    const QStringList lines = text.split(QLatin1Char('\n'));
    if (lines.size() < 3 || !lines.first().startsWith(QLatin1String("-----BEGIN ")))
        return info;
    const QString header = lines.first().trimmed();

    if (header == QLatin1String("-----BEGIN OPENSSH PRIVATE KEY-----")) {
        // openssh-key-v1: magic, cipher, kdf, kdf options, key count, public key blob.
        QByteArray b64;
        for (int i = 1; i < lines.size() && !lines[i].startsWith(QLatin1String("-----END")); ++i)
            b64 += lines[i].trimmed().toLatin1();
        const QByteArray blob = QByteArray::fromBase64(b64);
        const QByteArray magic("openssh-key-v1", 15); // includes the terminating NUL
        if (!blob.startsWith(magic))
            return info;
        int pos = magic.size();
        QByteArray cipher, kdf, kdfOptions, publicKey;
        if (!readString(blob, pos, cipher) || !readString(blob, pos, kdf) || !readString(blob, pos, kdfOptions))
            return info;
        pos += 4; // number of keys
        if (!readString(blob, pos, publicKey))
            return info;
        int keyPos = 0;
        QByteArray keyType;
        readString(publicKey, keyPos, keyType);
        info.valid = true;
        info.type = typeFromName(keyType);
        info.encrypted = cipher != "none";
        return info;
    }

    // PEM: "-----BEGIN RSA PRIVATE KEY-----", "EC", "DSA", PKCS#8 "PRIVATE KEY" /
    // "ENCRYPTED PRIVATE KEY". Traditional encrypted PEM has a Proc-Type header.
    if (!header.endsWith(QLatin1String("PRIVATE KEY-----")))
        return info;
    info.valid = true;
    if (header.contains(QLatin1String(" RSA ")))
        info.type = QStringLiteral("RSA");
    else if (header.contains(QLatin1String(" EC ")))
        info.type = QStringLiteral("ECDSA");
    else if (header.contains(QLatin1String(" DSA ")))
        info.type = QStringLiteral("DSA");
    info.encrypted = header.contains(QLatin1String("ENCRYPTED")) || text.contains(QLatin1String("Proc-Type: 4,ENCRYPTED"));
    return info;
}
