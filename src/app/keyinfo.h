#ifndef KEYINFO_H
#define KEYINFO_H

#include <QString>

// What can be told about a private key from its text without decrypting it.
struct KeyInfo
{
    bool valid = false;
    QString type;      // "Ed25519", "RSA", "ECDSA", "DSA" or "" if unknown
    bool encrypted = false;

    static KeyInfo inspect(const QString &keyText);
};

#endif // KEYINFO_H
