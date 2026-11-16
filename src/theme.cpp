#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>

static QString omarchyStateDir() {
    const QByteArray env = qgetenv("OMARCHY_STATE_DIR");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);
    return QDir::homePath() + QStringLiteral("/.local/state/omarchy");
}

Theme::Theme(QObject *parent) : QObject(parent) {
    reload();
    watch();
    loadRadius();
}

void Theme::reload() {
    m_themeDir = omarchyStateDir() + QStringLiteral("/current/theme");

    QColor accent(QStringLiteral("#FFD60A"));
    QColor background(QStringLiteral("#060B1E"));
    QColor foreground(QStringLiteral("#FFFFFF"));

    // colors.toml is a flat key = "value" file; parsing it with a regex keeps
    // the app dependency-free (no TOML library) and tolerant of the extra
    // keys Omarchy adds.
    QFile f(m_themeDir + QStringLiteral("/colors.toml"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        static const QRegularExpression re(
                QStringLiteral("^\\s*([A-Za-z0-9_]+)\\s*=\\s*\"(#[0-9A-Fa-f]{6,8})\""));
        while (!in.atEnd()) {
            const QString line = in.readLine();
            const auto m = re.match(line);
            if (!m.hasMatch())
                continue;
            const QString key = m.captured(1).toLower();
            const QColor c(m.captured(2));
            if (!c.isValid())
                continue;
            if (key == QLatin1String("accent"))
                accent = c;
            else if (key == QLatin1String("background"))
                background = c;
            else if (key == QLatin1String("foreground"))
                foreground = c;
        }
    }

    const bool changed = m_accent != accent || m_background != background
                         || m_foreground != foreground;
    m_accent = accent;
    m_background = background;
    m_foreground = foreground;
    if (changed)
        emit accentChanged();
}

void Theme::watch() {
    auto *watcher = new QFileSystemWatcher(this);
    const QString current = omarchyStateDir() + QStringLiteral("/current");
    watcher->addPath(current);
    watcher->addPath(omarchyStateDir());

    // A theme switch replaces `current` and the files behind it, so re-arm on
    // every signal rather than trusting the original paths to stay valid.
    auto onChanged = [this, watcher]() {
        reload();
        const QString currentDir = omarchyStateDir() + QStringLiteral("/current");
        if (!watcher->directories().contains(currentDir))
            watcher->addPath(currentDir);
        const QString themeDir = omarchyStateDir() + QStringLiteral("/current/theme");
        if (!watcher->directories().contains(themeDir))
            watcher->addPath(themeDir);
    };
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, onChanged);
    connect(watcher, &QFileSystemWatcher::fileChanged, this, onChanged);

    if (!watcher->directories().contains(m_themeDir))
        watcher->addPath(m_themeDir);
}

void Theme::loadRadius() {
    // Hyprland's decoration:rounding is 0 on a stock Omarchy install, so
    // corners are sharp unless the theme rounds them. Ask the compositor for
    // the effective value; fall back to 0 when it is not reachable.
    QProcess p;
    p.start(QStringLiteral("hyprctl"),
            {QStringLiteral("getoption"), QStringLiteral("decoration:rounding")});
    if (!p.waitForFinished(2000)) {
        p.kill();
        return;
    }
    const QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
    static const QRegularExpression re(QStringLiteral("int:\\s*(-?\\d+)"));
    const auto m = re.match(out);
    if (m.hasMatch()) {
        const int r = qMax(0, m.captured(1).toInt());
        if (m_radius != r) {
            m_radius = r;
            emit radiusChanged();
        }
    }
}
