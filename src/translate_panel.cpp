#include "translate_panel.h"

#include <QClipboard>
#include <QComboBox>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent>

TranslatePanel::TranslatePanel(QWidget* parent) : QWidget(parent) {
    language_ = new QComboBox(this);
    for (const auto& language : translationLanguages()) {
        language_->addItem(language.second, language.first);
    }
    const int defaultIndex = language_->findData(config_.targetLang);
    if (defaultIndex >= 0) language_->setCurrentIndex(defaultIndex);
    language_->setToolTip(QStringLiteral("翻译目标语言"));

    translateButton_ = new QPushButton(QStringLiteral("翻译"), this);
    translateButton_->setToolTip(QStringLiteral("翻译上方文本（Ctrl+T）"));

    copyButton_ = new QPushButton(QStringLiteral("复制译文"), this);
    copyButton_->setVisible(false);

    status_ = new QLabel(this);
    status_->setStyleSheet(QStringLiteral("color: #777;"));
    status_->setWordWrap(true);

    auto* controls = new QHBoxLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    controls->addWidget(new QLabel(QStringLiteral("翻译为"), this));
    controls->addWidget(language_);
    controls->addWidget(translateButton_);
    controls->addWidget(copyButton_);
    controls->addWidget(status_, 1);

    output_ = new QPlainTextEdit(this);
    output_->setReadOnly(true);
    output_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    output_->setPlaceholderText(QStringLiteral("译文会显示在这里…"));
    output_->setMinimumHeight(90);
    output_->setVisible(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addLayout(controls);
    layout->addWidget(output_, 1);

    connect(translateButton_, &QPushButton::clicked, this, [this] {
        translate(provider_ ? provider_() : QString());
    });
    connect(language_, &QComboBox::currentIndexChanged, this, [this](int) {
        updateTargetLanguage();
    });
    connect(copyButton_, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(output_->toPlainText());
        status_->setText(QStringLiteral("译文已复制到剪贴板"));
        status_->setStyleSheet(QStringLiteral("color: #2d7ff9;"));
    });
}

void TranslatePanel::setConfig(const TranslationConfig& config) {
    const bool same = config.engine == config_.engine &&
                      config.targetLang == config_.targetLang &&
                      config.libreUrl == config_.libreUrl &&
                      config.libreApiKey == config_.libreApiKey &&
                      config.cloudBaseUrl == config_.cloudBaseUrl &&
                      config.cloudModel == config_.cloudModel &&
                      config.cloudApiKey == config_.cloudApiKey &&
                      config.timeoutSec == config_.timeoutSec;
    config_ = config;
    if (same) return;

    const int index = language_->findData(config_.targetLang);
    if (index >= 0) language_->setCurrentIndex(index);
    ++generation_;
    output_->clear();
    output_->setVisible(false);
    copyButton_->setVisible(false);
    status_->clear();
    status_->setStyleSheet(QStringLiteral("color: #777;"));
}

void TranslatePanel::setTextProvider(std::function<QString()> provider) {
    provider_ = std::move(provider);
}

void TranslatePanel::updateTargetLanguage() {
    if (language_->currentData().isValid()) {
        config_.targetLang = language_->currentData().toString();
    }
}

void TranslatePanel::translate(const QString& text) {
    if (text.trimmed().isEmpty()) {
        status_->setText(QStringLiteral("没有可翻译的文字"));
        status_->setStyleSheet(QStringLiteral("color: #c0392b;"));
        return;
    }
    if (watcher_) return;  // a request is already in flight
    updateTargetLanguage();

    const quint64 generation = ++generation_;

    status_->setText(QStringLiteral("翻译中…"));
    status_->setStyleSheet(QStringLiteral("color: #777;"));
    translateButton_->setEnabled(false);
    copyButton_->setVisible(false);

    const TranslationConfig config = config_;
    auto* watcher = new QFutureWatcher<TranslationResult>(this);
    watcher_ = watcher;
    connect(watcher, &QFutureWatcher<TranslationResult>::finished, this,
            [this, watcher, generation] {
                const TranslationResult result = watcher->result();
                if (watcher_ == watcher) watcher_ = nullptr;
                watcher->deleteLater();
                translateButton_->setEnabled(true);
                if (generation != generation_) return;

                if (!result.error.isEmpty()) {
                    output_->setVisible(false);
                    copyButton_->setVisible(false);
                    status_->setText(QStringLiteral("翻译失败：%1").arg(result.error));
                    status_->setStyleSheet(QStringLiteral("color: #c0392b;"));
                    return;
                }

                output_->setPlainText(result.text);
                output_->setVisible(true);
                copyButton_->setVisible(true);
                status_->setText(
                    QStringLiteral("已翻译 · %1 → %2 · %3")
                        .arg(result.engine, translationLanguageName(config_.targetLang),
                             QStringLiteral("%1 字").arg(result.text.size())));
                status_->setStyleSheet(QStringLiteral("color: #2d7ff9;"));
            });
    watcher->setFuture(QtConcurrent::run(
        [text, config] { return translateText(text, config); }));
}

void TranslatePanel::clearResult() {
    ++generation_;
    output_->clear();
    output_->setVisible(false);
    copyButton_->setVisible(false);
    status_->clear();
    status_->setStyleSheet(QStringLiteral("color: #777;"));
}
