#include "platform.h"

#include "settings.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include <unistd.h>

QString Platform::serverName() {
    return QStringLiteral("mini-ocr-%1").arg(static_cast<qulonglong>(::getuid()));
}

QString Platform::uiFontFamily() {
    return QStringLiteral("Noto Sans CJK SC");
}

bool Platform::copyTextToClipboard(const QString& text) {
    // On Wayland, a client without keyboard focus cannot reliably own the
    // selection. wl-copy is a small helper that works regardless of focus.
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        QProcess process;
        process.start(QStringLiteral("wl-copy"), {});
        if (process.waitForStarted(500)) {
            process.write(text.toUtf8());
            process.closeWriteChannel();
            if (process.waitForFinished(1500) &&
                process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
                return true;
            }
            process.kill();
            process.waitForFinished(500);
        }
    }
    QGuiApplication::clipboard()->setText(text);
    return true;
}

QString autostartFilePath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
           QStringLiteral("/autostart/mini-ocr.desktop");
}

bool autostartEnabled() {
    return QFileInfo::exists(autostartFilePath());
}

bool setAutostartEnabled(bool enabled, QString* error) {
    const QString path = autostartFilePath();
    if (!enabled) {
        if (QFileInfo::exists(path) && !QFile::remove(path)) {
            if (error) *error = QStringLiteral("无法删除 %1").arg(path);
            return false;
        }
        return true;
    }

    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = QStringLiteral("无法创建 autostart 目录");
        return false;
    }

    QString exec = QCoreApplication::applicationFilePath();
    if (exec.contains(QLatin1Char(' '))) exec = QLatin1Char('"') + exec + QLatin1Char('"');

    const QString content =
        QStringLiteral("[Desktop Entry]\n"
                       "Type=Application\n"
                       "Name=Mini OCR\n"
                       "Exec=%1\n"
                       "Terminal=false\n"
                       "X-KDE-autostart-after=panel\n")
            .arg(exec);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法写入 %1").arg(path);
        return false;
    }
    file.write(content.toUtf8());
    if (!file.commit()) {
        if (error) *error = QStringLiteral("无法保存 %1").arg(path);
        return false;
    }
    return true;
}
