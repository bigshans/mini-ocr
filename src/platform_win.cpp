#include "platform.h"

#include "settings.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSettings>

namespace {
constexpr auto kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto kRunValue = "MiniOCR";
}  // namespace

QString Platform::serverName() {
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty()) user = QStringLiteral("user");
    user.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    return QStringLiteral("mini-ocr-%1").arg(user);
}

QString Platform::uiFontFamily() {
    return QStringLiteral("Microsoft YaHei UI");
}

bool Platform::copyTextToClipboard(const QString& text) {
    QGuiApplication::clipboard()->setText(text);
    return true;
}

QString autostartFilePath() {
    return QString::fromLatin1(kRunKey) + QLatin1Char('\\') +
           QString::fromLatin1(kRunValue);
}

bool autostartEnabled() {
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    return run.contains(QString::fromLatin1(kRunValue));
}

bool setAutostartEnabled(bool enabled, QString* error) {
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (enabled) {
        QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        exe = QLatin1Char('"') + exe + QLatin1Char('"');
        run.setValue(QString::fromLatin1(kRunValue), exe);
    } else {
        run.remove(QString::fromLatin1(kRunValue));
    }
    run.sync();
    if (run.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("无法更新 Windows 启动项注册表");
        return false;
    }
    return true;
}
