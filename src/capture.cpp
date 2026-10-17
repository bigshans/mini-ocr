#include "capture.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

QString Capture::captureRegion(QString* error) {
    const QString spectacle = QStandardPaths::findExecutable(QStringLiteral("spectacle"));
    if (spectacle.isEmpty()) {
        if (error) *error = QStringLiteral("找不到 spectacle，请先安装 KDE Spectacle");
        return {};
    }

    const QString out = QDir::temp().filePath(
        QStringLiteral("mini-ocr-%1.png")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));

    QProcess proc;
    proc.start(spectacle, {QStringLiteral("--region"),
                           QStringLiteral("--background"),
                           QStringLiteral("--nonotify"),
                           QStringLiteral("--output"), out});
    if (!proc.waitForStarted(5000)) {
        if (error) *error = QStringLiteral("无法启动 Spectacle");
        return {};
    }
    if (!proc.waitForFinished(180000)) {
        proc.kill();
        proc.waitForFinished(2000);
        if (error) *error = QStringLiteral("截图超时");
        QFile::remove(out);
        return {};
    }

    const QFileInfo fi(out);
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0 ||
        !fi.exists() || fi.size() == 0) {
        QFile::remove(out);
        return {};  // cancelled
    }
    return out;
}
