#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QObject>
#include <QString>

// Follows the Omarchy theme: the accent colour and Hyprland's window rounding.
// Omarchy replaces the theme files on every theme switch, so the watcher is
// re-armed after each change instead of watching a single inode.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor accent READ accent NOTIFY accentChanged)
    Q_PROPERTY(QColor background READ background NOTIFY accentChanged)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY accentChanged)
    Q_PROPERTY(int radius READ radius NOTIFY radiusChanged)

public:
    explicit Theme(QObject *parent = nullptr);

    QColor accent() const { return m_accent; }
    QColor background() const { return m_background; }
    QColor foreground() const { return m_foreground; }
    int radius() const { return m_radius; }

signals:
    void accentChanged();
    void radiusChanged();

private:
    void reload();
    void watch();
    void loadRadius();

    QColor m_accent = QColor(QStringLiteral("#FFD60A"));
    QColor m_background = QColor(QStringLiteral("#060B1E"));
    QColor m_foreground = QColor(QStringLiteral("#FFFFFF"));
    int m_radius = 0;
    QString m_themeDir;
};

#endif
