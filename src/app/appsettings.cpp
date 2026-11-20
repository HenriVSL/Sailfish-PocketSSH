#include "app/appsettings.h"

#include <QDir>
#include <QStandardPaths>

namespace {

QString settingsPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/settings.conf");
}

}

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
    , m_settings(settingsPath(), QSettings::IniFormat)
{
}

qreal AppSettings::fontPixelSize() const
{
    return m_settings.value(QStringLiteral("terminal/fontPixelSize"), 0.0).toReal();
}

void AppSettings::setFontPixelSize(qreal size)
{
    if (qFuzzyCompare(size + 1, fontPixelSize() + 1))
        return;
    m_settings.setValue(QStringLiteral("terminal/fontPixelSize"), size);
    emit fontPixelSizeChanged();
}

bool AppSettings::extraKeys() const
{
    return m_settings.value(QStringLiteral("terminal/extraKeys"), true).toBool();
}

void AppSettings::setExtraKeys(bool on)
{
    if (on == extraKeys())
        return;
    m_settings.setValue(QStringLiteral("terminal/extraKeys"), on);
    emit extraKeysChanged();
}

bool AppSettings::visualBell() const
{
    return m_settings.value(QStringLiteral("terminal/visualBell"), true).toBool();
}

void AppSettings::setVisualBell(bool on)
{
    if (on == visualBell())
        return;
    m_settings.setValue(QStringLiteral("terminal/visualBell"), on);
    emit visualBellChanged();
}
