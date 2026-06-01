#include "include/sys/linux/DesktopEntry.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTextStream>

namespace {

// Must match QGuiApplication::setDesktopFileName("Throne") so the running
// window's app_id resolves to this entry.
const QString kAppName = QStringLiteral("Throne");
const QString kDesktopFile = kAppName + QStringLiteral(".desktop");

// For AppImage, point at the outer image ($APPIMAGE); the extracted inner binary
// path disappears after exit. (Mirrors UrlScheme.cpp.)
QString execTarget() {
    const auto env = QProcessEnvironment::systemEnvironment();
    if (env.contains(QStringLiteral("APPIMAGE")))
        return env.value(QStringLiteral("APPIMAGE"));
    return QApplication::applicationFilePath();
}

QString iconValue() {
    // The portable build ships Throne.png next to the binary; prefer its
    // absolute path, otherwise fall back to a themed icon name.
    const QString bundled = QApplication::applicationDirPath() + QStringLiteral("/Throne.png");
    return QFile::exists(bundled) ? bundled : kAppName;
}

// True if a desktop entry already exists in a *system* applications directory
// (e.g. installed by the .deb); in that case we leave identity resolution to it.
bool systemEntryExists() {
    const QString userDir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    const auto dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &dir : dirs) {
        if (dir == userDir)
            continue; // skip the per-user dir; we only care about system ones
        if (QFile::exists(dir + QStringLiteral("/") + kDesktopFile))
            return true;
    }
    return false;
}

} // namespace

void DesktopEntry_Ensure() {
    if (systemEntryExists())
        return;

    const QString userDir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (userDir.isEmpty())
        return;

    const QString path = userDir + QStringLiteral("/") + kDesktopFile;
    const QString execLine = QStringLiteral("Exec=\"%1\"").arg(execTarget());

    // Skip the rewrite (and the update-desktop-database call) when our entry is
    // already present and points at the current binary. Rewrite if the portable
    // folder moved, so neither the portal nor the launcher reference a stale path.
    if (QFile::exists(path)) {
        QFile existing(path);
        if (existing.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(existing.readAll());
            existing.close();
            if (content.contains(execLine))
                return;
        }
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream ts(&f);
    ts << "[Desktop Entry]\n"
       << "Type=Application\n"
       << "Name=" << kAppName << "\n"
       << "Comment=Qt based cross-platform GUI proxy configuration manager (backend: sing-box)\n"
       << execLine << "\n"
       << "Icon=" << iconValue() << "\n"
       << "Terminal=false\n"
       << "Categories=Network;Application;\n"
       << "StartupWMClass=" << kAppName << "\n";
    ts.flush();
    f.close();

    // Best-effort DB refresh; the tool may be absent on minimal systems.
    QProcess::execute(QStringLiteral("update-desktop-database"), {userDir});
}
