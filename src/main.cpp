#include "batch_window.h"
#include "capture.h"
#include "history.h"
#include "hotkey.h"
#include "ocr_backend.h"
#include "ocr_engine.h"
#include "platform.h"
#include "result_window.h"
#include "settings.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkProxyFactory>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>
#include <QtConcurrent>

#include <memory>

namespace {

QString findModelDir(const QString& preferred = QString()) {
    QStringList candidates;
    const QByteArray env = qgetenv("MINI_OCR_MODEL_DIR");
    if (!env.isEmpty()) candidates << QString::fromLocal8Bit(env);
    if (!preferred.isEmpty()) candidates << preferred;
    const QString configured = configuredModelDir();
    if (!configured.isEmpty()) candidates << configured;
    candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/models");
    candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/../share/mini-ocr/models");
#ifdef MINI_OCR_DEFAULT_MODEL_DIR
    candidates << QStringLiteral(MINI_OCR_DEFAULT_MODEL_DIR);
#endif

    for (const QString& dir : candidates) {
        if (QFileInfo::exists(dir + QStringLiteral("/ch_PP-OCRv5_det_mobile.onnx")) &&
            QFileInfo::exists(dir + QStringLiteral("/ch_PP-OCRv5_rec_mobile.onnx")) &&
            QFileInfo::exists(dir + QStringLiteral("/ppocrv5_dict.txt"))) {
            return QDir(dir).absolutePath();
        }
    }
    return {};
}

OcrConfig ocrConfigFrom(const AppSettings& settings) {
    OcrConfig config;
    config.modelDir = findModelDir(settings.modelDir);
    config.tesseractLang = settings.tesseractLang;
    config.cloudBaseUrl = settings.cloudBaseUrl;
    config.cloudModel = settings.cloudModel;
    config.cloudApiKey = cloudApiKeyFor(settings);
    config.cloudPrompt = settings.cloudPrompt;
    config.cloudTimeoutSec = settings.cloudTimeoutSec;
    return config;
}

QIcon makeAppIcon() {
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QStringLiteral("#2d7ff9")));
    p.drawRoundedRect(QRectF(4, 4, 56, 56), 14, 14);

    p.setPen(QPen(Qt::white, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const int m = 17, len = 9;
    p.drawLine(m, m, m + len, m);
    p.drawLine(m, m, m, m + len);
    p.drawLine(64 - m, m, 64 - m - len, m);
    p.drawLine(64 - m, m, 64 - m, m + len);
    p.drawLine(m, 64 - m, m + len, 64 - m);
    p.drawLine(m, 64 - m, m, 64 - m - len);
    p.drawLine(64 - m, 64 - m, 64 - m - len, 64 - m);
    p.drawLine(64 - m, 64 - m, 64 - m, 64 - m - len);

    p.setPen(Qt::white);
    QFont f(Platform::uiFontFamily(), 22, QFont::Bold);
    p.setFont(f);
    p.drawText(QRectF(0, 0, 64, 64), Qt::AlignCenter, QStringLiteral("文"));
    return QIcon(pm);
}

QImage loadImageFile(const QString& path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    return reader.read();
}

struct OcrResult {
    QVector<OcrLine> lines;
    QString error;
    qint64 elapsedMs = 0;
};

}  // namespace

class App : public QObject {
public:
    explicit App(QApplication& app) : app_(app) {}

    bool init(const QString& initialCommand) {
        settings_ = loadSettings();
        autoCopy_ = settings_.autoCopy;

        backend_ = std::move(createOcrBackend(settings_.ocrBackend));
        if (!backend_) {
            modelError_ = QStringLiteral("所选 OCR 引擎不可用，已回退到 PP-OCRv5。");
            settings_.ocrBackend = QStringLiteral("ppocr");
            backend_ = std::move(createOcrBackend(settings_.ocrBackend));
        }

        if (backend_) {
            OcrConfig config = ocrConfigFrom(settings_);

            QElapsedTimer timer;
            timer.start();
            QString error;
            if (backend_->load(config, &error)) {
                backend_->setMinScore(settings_.minScore);
                modelError_.clear();
                qInfo("loaded OCR backend %s in %lld ms",
                      qUtf8Printable(backend_->id()),
                      static_cast<long long>(timer.elapsed()));
            } else {
                modelError_ = QStringLiteral("OCR 引擎加载失败：%1").arg(error);
            }
        } else {
            modelError_ = QStringLiteral("没有可用的 OCR 引擎");
        }

        QString historyError;
        if (!history_.open(&historyError)) {
            qWarning("history disabled: %s", qUtf8Printable(historyError));
        } else {
            history_.setLimit(settings_.historyLimit);
        }

        setupTray();
        updateTrayTooltip();
        if (!setupServer()) return false;

        hotkey_ = std::make_unique<GlobalHotkey>();
        hotkey_->setCallback([this] { captureNow(); });
        QString hotkeyError;
        if (!hotkey_->registerHotkey(QStringLiteral("Ctrl+Alt+O"), &hotkeyError)) {
#ifdef Q_OS_WIN
            tray_.showMessage(QStringLiteral("Mini OCR"),
                              QStringLiteral("全局快捷键 Ctrl+Alt+O 注册失败：%1")
                                  .arg(hotkeyError),
                              QSystemTrayIcon::Warning, 6000);
#endif
        }

        if (initialCommand == QStringLiteral("capture")) {
            QTimer::singleShot(0, this, [this] { captureNow(); });
        } else if (initialCommand == QStringLiteral("history")) {
            QTimer::singleShot(0, this, [this] { showHistory(); });
        } else if (initialCommand == QStringLiteral("settings")) {
            QTimer::singleShot(0, this, [this] { showSettings(); });
        } else if (initialCommand == QStringLiteral("upload")) {
            QTimer::singleShot(0, this, [this] { openUploadWindow(); });
        } else if (!modelError_.isEmpty()) {
            tray_.showMessage(QStringLiteral("Mini OCR"), modelError_,
                              QSystemTrayIcon::Warning, 6000);
        }
        return true;
    }

    void openUploadWindow() {
        if (busy_) {
            tray_.showMessage(QStringLiteral("Mini OCR"), QStringLiteral("正在识别中…"),
                              QSystemTrayIcon::Information, 1500);
            return;
        }
        if (!backend_ || !backend_->isLoaded()) {
            QMessageBox::critical(nullptr, QStringLiteral("Mini OCR"), modelError_);
            return;
        }
        if (!batchWindow_) {
            batchWindow_ = new BatchWindow();
            batchWindow_->setStartCallback([this] { startBatchRun(); });
            batchWindow_->setCancelCallback([this] { cancelBatch(); });
            connect(batchWindow_, &QObject::destroyed, this,
                    [this] { batchWindow_ = nullptr; });
            batchWindow_->show();
        }
        batchWindow_->raise();
        batchWindow_->activateWindow();
    }

    void openImages() { openUploadWindow(); }

    void openFiles(const QStringList& paths) {
        if (busy_) {
            tray_.showMessage(QStringLiteral("Mini OCR"), QStringLiteral("正在识别中…"),
                              QSystemTrayIcon::Information, 1500);
            return;
        }
        if (!backend_ || !backend_->isLoaded()) {
            QMessageBox::critical(nullptr, QStringLiteral("Mini OCR"), modelError_);
            return;
        }

        QStringList files;
        for (const QString& path : paths) {
            const QFileInfo info(path);
            if (info.isFile()) files << info.absoluteFilePath();
        }
        if (files.isEmpty()) {
            tray_.showMessage(QStringLiteral("Mini OCR"),
                              QStringLiteral("没有可读取的图片"),
                              QSystemTrayIcon::Information, 2500);
            return;
        }

        if (files.size() == 1) {
            QImage image = loadImageFile(files.first());
            if (image.isNull()) {
                QMessageBox::warning(nullptr, QStringLiteral("Mini OCR"),
                                     QStringLiteral("无法读取图片：%1").arg(files.first()));
                return;
            }
            startSingleOcr(image);
            return;
        }

        if (batchWindow_) batchWindow_->close();
        batchWindow_ = new BatchWindow();
        batchWindow_->setStartCallback([this] { startBatchRun(); });
        batchWindow_->setCancelCallback([this] { cancelBatch(); });
        connect(batchWindow_, &QObject::destroyed, this, [this] { batchWindow_ = nullptr; });
        batchWindow_->appendFiles(files);
        batchWindow_->show();
        batchWindow_->raise();
        batchWindow_->activateWindow();
        startBatchRun();
    }

private:
    void setupTray() {
        tray_.setIcon(makeAppIcon());
        tray_.setToolTip(QStringLiteral("Mini OCR — 截图识别"));
        QApplication::setWindowIcon(makeAppIcon());

        auto* menu = new QMenu;
        auto* captureAction = menu->addAction(QStringLiteral("截图识别    Ctrl+Alt+O"));
        auto* openAction = menu->addAction(QStringLiteral("图片 OCR…"));
        auto* historyAction = menu->addAction(QStringLiteral("历史记录"));
        auto* settingsAction = menu->addAction(QStringLiteral("设置…"));
        menu->addSeparator();
        autoCopyAction_ = menu->addAction(QStringLiteral("识别后自动复制"));
        autoCopyAction_->setCheckable(true);
        autoCopyAction_->setChecked(autoCopy_);
        menu->addSeparator();
        auto* quitAction = menu->addAction(QStringLiteral("退出"));

        connect(captureAction, &QAction::triggered, this, [this] { captureNow(); });
        connect(openAction, &QAction::triggered, this, [this] { openImages(); });
        connect(historyAction, &QAction::triggered, this, [this] { showHistory(); });
        connect(settingsAction, &QAction::triggered, this, [this] { showSettings(); });
        connect(autoCopyAction_, &QAction::toggled, this, [this](bool on) {
            autoCopy_ = on;
            settings_.autoCopy = on;
            saveSettings(settings_);
        });
        connect(quitAction, &QAction::triggered, this, [this] { quitApp(); });
        connect(&tray_, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
                    if (reason == QSystemTrayIcon::Trigger ||
                        reason == QSystemTrayIcon::DoubleClick) {
                        captureNow();
                    }
                });

        tray_.setContextMenu(menu);
        tray_.show();
    }

    bool setupServer() {
        server_ = new QLocalServer(this);
        server_->setSocketOptions(QLocalServer::UserAccessOption);
        QLocalServer::removeServer(Platform::serverName());
        if (!server_->listen(Platform::serverName())) {
            qWarning("cannot listen on %s: %s", qUtf8Printable(Platform::serverName()),
                     qUtf8Printable(server_->errorString()));
            return false;
        }
        connect(server_, &QLocalServer::newConnection, this, [this] {
            while (QLocalSocket* socket = server_->nextPendingConnection()) {
                connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                    const QByteArray data = socket->readAll();
                    for (const QByteArray& raw : data.split('\n')) {
                        const QString cmd = QString::fromUtf8(raw).trimmed();
                        if (!cmd.isEmpty()) handleCommand(cmd);
                    }
                    socket->disconnectFromServer();
                });
                connect(socket, &QLocalSocket::disconnected,
                        socket, &QLocalSocket::deleteLater);
            }
        });
        return true;
    }

    void handleCommand(const QString& cmd) {
        if (cmd.startsWith(QStringLiteral("openfiles "))) {
            const QJsonDocument doc =
                QJsonDocument::fromJson(cmd.mid(10).toUtf8());
            if (doc.isArray()) {
                QStringList paths;
                for (const QJsonValue& value : doc.array()) {
                    if (value.isString()) paths << value.toString();
                }
                openFiles(paths);
            }
            return;
        }
        if (cmd == QStringLiteral("capture")) {
            captureNow();
        } else if (cmd == QStringLiteral("history")) {
            showHistory();
        } else if (cmd == QStringLiteral("settings")) {
            showSettings();
        } else if (cmd == QStringLiteral("upload")) {
            openUploadWindow();
        } else if (cmd == QStringLiteral("quit")) {
            quitApp();
        }
    }

    void updateTrayTooltip() {
        if (backend_) {
            tray_.setToolTip(QStringLiteral("Mini OCR — %1").arg(backend_->displayName()));
        } else {
            tray_.setToolTip(QStringLiteral("Mini OCR"));
        }
    }

    void quitApp() {
        if (hotkey_) hotkey_->unregister();
        batchRunning_ = false;
        batchCancelled_ = true;
        if (batchWindow_) batchWindow_->finishRun(true);
        if (ocrWatcher_ && !ocrWatcher_->isFinished()) {
            QThreadPool::globalInstance()->waitForDone(2000);
        }
        const auto windows = QApplication::topLevelWidgets();
        for (QWidget* window : windows) window->close();
        qApp->quit();
    }

    void showSettings() {
        if (busy_) {
            tray_.showMessage(QStringLiteral("Mini OCR"),
                              QStringLiteral("正在识别中，请稍后再打开设置"),
                              QSystemTrayIcon::Information, 2000);
            return;
        }
        settingsOpen_ = true;
        const QString currentInfo =
            backend_ ? backend_->info() : findModelDir(settings_.modelDir);
        SettingsDialog dialog(settings_, currentInfo, QApplication::activeWindow());
        const int dialogResult = dialog.exec();
        settingsOpen_ = false;
        if (dialogResult != QDialog::Accepted) return;

        const AppSettings next = dialog.settings();
        const bool backendChanged = next.ocrBackend != settings_.ocrBackend ||
                                    next.modelDir != settings_.modelDir ||
                                    next.tesseractLang != settings_.tesseractLang ||
                                    next.cloudPreset != settings_.cloudPreset ||
                                    next.cloudBaseUrl != settings_.cloudBaseUrl ||
                                    next.cloudModel != settings_.cloudModel ||
                                    next.deepseekApiKey != settings_.deepseekApiKey ||
                                    next.siliconflowApiKey != settings_.siliconflowApiKey ||
                                    next.customApiKey != settings_.customApiKey ||
                                    next.cloudPrompt != settings_.cloudPrompt ||
                                    next.cloudTimeoutSec != settings_.cloudTimeoutSec;

        if (backendChanged) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            auto candidate = createOcrBackend(next.ocrBackend);
            QString error;
            if (!candidate) {
                QApplication::restoreOverrideCursor();
                QMessageBox::critical(nullptr, QStringLiteral("Mini OCR"),
                                      QStringLiteral("该 OCR 引擎不可用，设置未保存。"));
                return;
            }
            OcrConfig config = ocrConfigFrom(next);
            if (!candidate->load(config, &error)) {
                QApplication::restoreOverrideCursor();
                QMessageBox::critical(nullptr, QStringLiteral("Mini OCR"),
                                      QStringLiteral("OCR 引擎加载失败，设置未保存：\n%1")
                                          .arg(error));
                return;
            }
            candidate->setMinScore(next.minScore);
            backend_ = std::move(candidate);
            modelError_.clear();
            updateTrayTooltip();
            QApplication::restoreOverrideCursor();
        } else if (backend_) {
            backend_->setMinScore(next.minScore);
        }

        saveSettings(next);
        settings_ = next;
        autoCopy_ = next.autoCopy;
        if (autoCopyAction_) {
            const QSignalBlocker blocker(autoCopyAction_);
            autoCopyAction_->setChecked(autoCopy_);
        }
        history_.setLimit(next.historyLimit);

        QString autostartError;
        if (!setAutostartEnabled(next.autostart, &autostartError)) {
            QMessageBox::warning(nullptr, QStringLiteral("Mini OCR"), autostartError);
        }
    }

    void captureNow() {
        if (settingsOpen_) {
            tray_.showMessage(QStringLiteral("Mini OCR"),
                              QStringLiteral("请先关闭设置窗口"),
                              QSystemTrayIcon::Information, 1500);
            return;
        }
        if (busy_) {
            tray_.showMessage(QStringLiteral("Mini OCR"), QStringLiteral("正在识别中…"),
                              QSystemTrayIcon::Information, 1500);
            return;
        }
        if (!backend_ || !backend_->isLoaded()) {
            QMessageBox::critical(nullptr, QStringLiteral("Mini OCR"), modelError_);
            return;
        }

        busy_ = true;
        QString error;
        const QString path = Capture::captureRegion(&error);
        if (path.isEmpty()) {
            busy_ = false;
            if (!error.isEmpty()) {
                tray_.showMessage(QStringLiteral("Mini OCR"), error,
                                  QSystemTrayIcon::Warning, 4000);
            }
            return;
        }

        QImage image(path);
        QFile::remove(path);
        if (image.isNull()) {
            busy_ = false;
            tray_.showMessage(QStringLiteral("Mini OCR"), QStringLiteral("截图读取失败"),
                              QSystemTrayIcon::Warning, 3000);
            return;
        }

        startSingleOcr(image);
    }

    void presentOcrResult(const OcrResult& result) {
        if (!result.error.isEmpty()) {
            tray_.showMessage(QStringLiteral("Mini OCR"),
                              QStringLiteral("识别失败：%1").arg(result.error),
                              QSystemTrayIcon::Critical, 5000);
            return;
        }
        if (result.lines.isEmpty()) {
            tray_.showMessage(QStringLiteral("Mini OCR"),
                              QStringLiteral("未识别到文字"),
                              QSystemTrayIcon::Information, 2500);
            return;
        }

        QStringList parts;
        parts.reserve(result.lines.size());
        for (const auto& line : result.lines) parts << line.text;
        const QString text = parts.join(QLatin1Char('\n'));

        if (history_.isOpen()) history_.add(text);

        const bool copied = autoCopy_ && Platform::copyTextToClipboard(text);
        if (resultWindow_) resultWindow_->close();
        resultWindow_ = new ResultWindow(text, result.lines, copied);
        resultWindow_->show();
        resultWindow_->raise();
        resultWindow_->activateWindow();
        qInfo("OCR %lld ms, %lld lines", static_cast<long long>(result.elapsedMs),
              static_cast<long long>(result.lines.size()));
    }

    void startSingleOcr(const QImage& image) {
        busy_ = true;
        auto* watcher = new QFutureWatcher<OcrResult>(this);
        ocrWatcher_ = watcher;
        connect(watcher, &QFutureWatcher<OcrResult>::finished, this, [this, watcher] {
            const OcrResult result = watcher->result();
            ocrWatcher_ = nullptr;
            watcher->deleteLater();
            busy_ = false;
            presentOcrResult(result);
        });

        const auto backend = backend_;
        watcher->setFuture(QtConcurrent::run([backend, image] {
            QElapsedTimer timer;
            timer.start();
            OcrResult result;
            const cv::Mat bgr = OcrEngine::qImageToBgr(image);
            result.lines = backend->recognize(bgr, &result.error);
            result.elapsedMs = timer.elapsed();
            return result;
        }));
    }

    void startBatchRun() {
        if (!batchWindow_) return;
        const QStringList paths = batchWindow_->paths();
        if (paths.isEmpty()) {
            tray_.showMessage(QStringLiteral("Mini OCR"),
                              QStringLiteral("请先添加图片"),
                              QSystemTrayIcon::Information, 2000);
            return;
        }
        if (busy_) {
            tray_.showMessage(QStringLiteral("Mini OCR"), QStringLiteral("正在识别中…"),
                              QSystemTrayIcon::Information, 1500);
            return;
        }
        if (!backend_ || !backend_->isLoaded()) {
            QMessageBox::critical(nullptr, QStringLiteral("Mini OCR"), modelError_);
            if (batchWindow_) batchWindow_->finishRun(true);
            return;
        }

        batchPaths_ = paths;
        batchCancelled_ = false;
        batchFinished_ = false;
        batchIndex_ = 0;
        batchRunning_ = true;
        batchInFlight_ = false;
        busy_ = true;
        batchWindow_->resetForRun();
        processNextBatchItem();
    }

    void processNextBatchItem() {
        if (!batchRunning_) {
            finishBatch();
            return;
        }
        if (batchIndex_ >= batchPaths_.size()) {
            finishBatch();
            return;
        }
        if (!batchWindow_) {
            batchRunning_ = false;
            batchInFlight_ = false;
            busy_ = false;
            return;
        }

        const int index = batchIndex_;
        const QString path = batchPaths_[index];
        batchWindow_->setStatus(index, QStringLiteral("识别中…"));

        QImage image = loadImageFile(path);
        if (image.isNull()) {
            batchWindow_->setError(index, QStringLiteral("无法读取图片"));
            batchWindow_->setProgress(index + 1);
            ++batchIndex_;
            QTimer::singleShot(0, this, [this] { processNextBatchItem(); });
            return;
        }

        batchInFlight_ = true;
        const auto backend = backend_;
        auto* watcher = new QFutureWatcher<OcrResult>(this);
        connect(watcher, &QFutureWatcher<OcrResult>::finished, this,
                [this, watcher, index] {
                    const OcrResult result = watcher->result();
                    watcher->deleteLater();
                    batchInFlight_ = false;

                    if (batchWindow_) {
                        if (!result.error.isEmpty()) {
                            batchWindow_->setError(index, result.error);
                        } else {
                            QStringList parts;
                            for (const auto& line : result.lines) parts << line.text;
                            const QString text = parts.join(QLatin1Char('\n'));
                            if (text.isEmpty()) {
                                batchWindow_->setError(index, QStringLiteral("未识别到文字"));
                            } else {
                                batchWindow_->setResult(index, text);
                                if (history_.isOpen()) history_.add(text);
                            }
                        }
                        batchWindow_->setProgress(index + 1);
                    }

                    ++batchIndex_;
                    if (batchRunning_) {
                        QTimer::singleShot(0, this, [this] { processNextBatchItem(); });
                    } else {
                        finishBatch();
                    }
                });

        watcher->setFuture(QtConcurrent::run([backend, image] {
            QElapsedTimer timer;
            timer.start();
            OcrResult result;
            const cv::Mat bgr = OcrEngine::qImageToBgr(image);
            result.lines = backend->recognize(bgr, &result.error);
            result.elapsedMs = timer.elapsed();
            return result;
        }));
    }

    void cancelBatch() {
        batchCancelled_ = true;
        batchRunning_ = false;
        if (batchWindow_) batchWindow_->markCancelling();
        if (!batchInFlight_) finishBatch();
    }

    void finishBatch() {
        if (batchFinished_) return;
        batchFinished_ = true;
        const bool cancelled = batchCancelled_;
        batchCancelled_ = false;
        batchRunning_ = false;
        batchInFlight_ = false;
        busy_ = false;
        if (batchWindow_) batchWindow_->finishRun(cancelled);
    }

    void showHistory() {
        if (!history_.isOpen()) {
            QMessageBox::warning(nullptr, QStringLiteral("Mini OCR"),
                                 QStringLiteral("历史记录不可用"));
            return;
        }
        if (!historyWindow_) {
            historyWindow_ = new HistoryDialog(&history_);
        }
        historyWindow_->show();
        historyWindow_->raise();
        historyWindow_->activateWindow();
    }

    QApplication& app_;
    std::shared_ptr<IOcrBackend> backend_;
    HistoryStore history_;
    QSystemTrayIcon tray_;
    std::unique_ptr<GlobalHotkey> hotkey_;
    QLocalServer* server_ = nullptr;
    QAction* autoCopyAction_ = nullptr;
    QPointer<ResultWindow> resultWindow_;
    QPointer<HistoryDialog> historyWindow_;
    QPointer<BatchWindow> batchWindow_;
    QPointer<QFutureWatcher<OcrResult>> ocrWatcher_;
    QStringList batchPaths_;
    int batchIndex_ = 0;
    bool batchRunning_ = false;
    bool batchInFlight_ = false;
    bool batchCancelled_ = false;
    bool batchFinished_ = false;
    AppSettings settings_;
    QString modelError_;
    bool autoCopy_ = true;
    bool busy_ = false;
    bool settingsOpen_ = false;
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("mini-ocr"));
    QApplication::setApplicationDisplayName(QStringLiteral("Mini OCR"));
#ifdef Q_OS_LINUX
    QApplication::setDesktopFileName(QStringLiteral("mini-ocr"));
#endif
    QApplication::setQuitOnLastWindowClosed(false);
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    const QStringList args = app.arguments();
    const QString cmd = args.size() > 1 ? args[1] : QString();

    AppSettings cliSettings = loadSettings();
    auto loadCliBackend = [&]() -> std::unique_ptr<IOcrBackend> {
        auto backend = createOcrBackend(cliSettings.ocrBackend);
        if (!backend) {
            fprintf(stderr, "OCR backend '%s' is not available\n",
                    qUtf8Printable(cliSettings.ocrBackend));
            return nullptr;
        }
        OcrConfig config = ocrConfigFrom(cliSettings);
        QString error;
        if (!backend->load(config, &error)) {
            fprintf(stderr, "%s\n", qUtf8Printable(error));
            return nullptr;
        }
        backend->setMinScore(cliSettings.minScore);
        return backend;
    };

    // Developer CLI: mini-ocr ocr <image> (does not start the tray app)
    if (cmd == QStringLiteral("ocr")) {
        if (args.size() < 3) {
            fprintf(stderr, "usage: mini-ocr ocr <image>\n");
            return 2;
        }
        auto backend = loadCliBackend();
        if (!backend) return 1;
        const QImage image = loadImageFile(args[2]);
        if (image.isNull()) {
            fprintf(stderr, "cannot read image: %s\n", qUtf8Printable(args[2]));
            return 1;
        }
        QString error;
        const auto lines = backend->recognize(OcrEngine::qImageToBgr(image), &error);
        if (!error.isEmpty()) fprintf(stderr, "%s\n", qUtf8Printable(error));
        for (const auto& line : lines) {
            printf("[%.3f] %s\n", line.score, line.text.toUtf8().constData());
        }
        return error.isEmpty() ? 0 : 1;
    }

    // Developer/user helper: mini-ocr show <image> (recognize an existing file)
    if (cmd == QStringLiteral("show")) {
        if (args.size() < 3) {
            fprintf(stderr, "usage: mini-ocr show <image>\n");
            return 2;
        }
        auto backend = loadCliBackend();
        if (!backend) return 1;
        const QImage image = loadImageFile(args[2]);
        if (image.isNull()) {
            fprintf(stderr, "cannot read image: %s\n", qUtf8Printable(args[2]));
            return 1;
        }
        QString error;
        const auto lines = backend->recognize(OcrEngine::qImageToBgr(image), &error);
        if (!error.isEmpty()) {
            fprintf(stderr, "%s\n", qUtf8Printable(error));
            return 1;
        }
        QStringList parts;
        for (const auto& line : lines) parts << line.text;
        auto* window = new ResultWindow(parts.join(QLatin1Char('\n')), lines, false);
        window->show();
        return app.exec();
    }

    QStringList fileArgs;
    if (cmd == QStringLiteral("files")) {
        if (args.size() < 3) {
            fprintf(stderr, "usage: mini-ocr files <image...>\n");
            return 2;
        }
        for (int i = 2; i < args.size(); ++i) fileArgs << args[i];
    }

    QString forwardCommand = cmd;
    if (cmd == QStringLiteral("files")) {
        QJsonArray files;
        for (const QString& file : fileArgs) files.append(file);
        forwardCommand = QStringLiteral("openfiles ") +
                         QString::fromUtf8(QJsonDocument(files).toJson(QJsonDocument::Compact));
    }

    // Forward to the already running instance if there is one.
    QLocalSocket socket;
    socket.connectToServer(Platform::serverName());
    if (socket.waitForConnected(300)) {
        socket.write((forwardCommand.isEmpty() ? QStringLiteral("ping") : forwardCommand)
                         .toUtf8() + '\n');
        socket.waitForBytesWritten(500);
        return 0;
    }

    // No running instance: nothing to quit.
    if (cmd == QStringLiteral("quit")) return 0;

    App application(app);
    if (!application.init(cmd == QStringLiteral("files") ? QString() : cmd)) {
        fprintf(stderr, "mini-ocr: failed to start\n");
        return 1;
    }
    if (cmd == QStringLiteral("files")) application.openFiles(fileArgs);
    return app.exec();
}
