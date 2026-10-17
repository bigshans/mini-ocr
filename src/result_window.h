#pragma once

#include <QWidget>
#include <QVector>

#include "ocr_backend.h"

class QLabel;
class QPlainTextEdit;

class ResultWindow : public QWidget {
public:
    ResultWindow(const QString& text, const QVector<OcrLine>& lines,
                 bool alreadyCopied, QWidget* parent = nullptr);

    void copyToClipboard();

private:

    QPlainTextEdit* edit_;
    QLabel* status_;
};
