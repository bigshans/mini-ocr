#include "capture.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QTimer>
#include <QUrl>
#include <QUuid>

QString Capture::captureRegion(QString* error) {
    // Windows 10/11 ships a native region-selection overlay. It handles
    // multi-monitor and per-monitor DPI correctly, and puts the result on the
    // clipboard. This is the first Windows implementation; a fully native
    // overlay can replace it later without touching the rest of the app.
    QClipboard* clipboard = QGuiApplication::clipboard();
    bool gotImage = false;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    const QMetaObject::Connection changed = QObject::connect(
        clipboard, &QClipboard::dataChanged, &loop, [&] {
            if (!clipboard->image().isNull()) {
                gotImage = true;
                loop.quit();
            }
        });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(60000);

    if (!QDesktopServices::openUrl(QUrl(QStringLiteral("ms-screenclip:")))) {
        QObject::disconnect(changed);
        if (error) *error = QStringLiteral("无法启动 Windows 截图工具");
        return {};
    }

    loop.exec();
    QObject::disconnect(changed);
    timeout.stop();

    if (!gotImage) {
        if (error) *error = QStringLiteral("截图已取消或超时");
        return {};
    }

    const QImage image = clipboard->image();
    if (image.isNull()) {
        if (error) *error = QStringLiteral("没有从剪贴板读取到图片");
        return {};
    }

    const QString out = QDir::temp().filePath(
        QStringLiteral("mini-ocr-%1.png")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!image.save(out, "PNG")) {
        if (error) *error = QStringLiteral("截图保存失败");
        return {};
    }
    return out;
}
