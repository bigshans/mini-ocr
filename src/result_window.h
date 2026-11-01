#pragma once

#include <QWidget>
#include <QVector>

#include "ocr_backend.h"

class QLabel;
class QPlainTextEdit;
class TranslatePanel;
struct TranslationConfig;

class ResultWindow : public QWidget {
public:
    ResultWindow(const QString& text, const QVector<OcrLine>& lines,
                 bool alreadyCopied, const TranslationConfig& translation,
                 QWidget* parent = nullptr);

    void copyToClipboard();
    void setTranslationConfig(const TranslationConfig& translation);

private:
    QPlainTextEdit* edit_;
    QLabel* status_;
    TranslatePanel* translatePanel_ = nullptr;
};
