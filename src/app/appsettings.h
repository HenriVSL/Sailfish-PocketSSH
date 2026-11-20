#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QObject>
#include <QSettings>

// User preferences, in settings.conf in the app's config directory. Sailjail only lets the
// app write inside ~/.config/<organization>/<application>/, not QSettings' default
// ~/.config/<organization>/<application>.conf.
class AppSettings : public QObject
{
    Q_OBJECT
    // Terminal font size in pixels; 0 = the TerminalItem default.
    Q_PROPERTY(qreal fontPixelSize READ fontPixelSize WRITE setFontPixelSize NOTIFY fontPixelSizeChanged)
    Q_PROPERTY(bool extraKeys READ extraKeys WRITE setExtraKeys NOTIFY extraKeysChanged)
    Q_PROPERTY(bool visualBell READ visualBell WRITE setVisualBell NOTIFY visualBellChanged)

public:
    explicit AppSettings(QObject *parent = nullptr);

    qreal fontPixelSize() const;
    void setFontPixelSize(qreal size);
    bool extraKeys() const;
    void setExtraKeys(bool on);
    bool visualBell() const;
    void setVisualBell(bool on);

signals:
    void fontPixelSizeChanged();
    void extraKeysChanged();
    void visualBellChanged();

private:
    QSettings m_settings;
};

#endif // APPSETTINGS_H
