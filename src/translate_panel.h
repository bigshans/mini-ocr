#pragma once

#include <QWidget>

#include <functional>

#include "translate.h"

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
template <typename T>
class QFutureWatcher;

class TranslatePanel : public QWidget {
public:
    explicit TranslatePanel(QWidget* parent = nullptr);

    void setConfig(const TranslationConfig& config);
    void setTextProvider(std::function<QString()> provider);
    void translate(const QString& text);
    void clearResult();

private:
    void updateTargetLanguage();

    TranslationConfig config_;
    std::function<QString()> provider_;
    QComboBox* language_ = nullptr;
    QPushButton* translateButton_ = nullptr;
    QPushButton* copyButton_ = nullptr;
    QPlainTextEdit* output_ = nullptr;
    QLabel* status_ = nullptr;
    QFutureWatcher<TranslationResult>* watcher_ = nullptr;
    quint64 generation_ = 0;
};
