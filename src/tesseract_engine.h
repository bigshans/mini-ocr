#pragma once

#include "ocr_backend.h"

#include <QString>
#include <QStringList>

#include <tesseract/capi.h>

class TesseractEngine : public IOcrBackend {
public:
    ~TesseractEngine() override;

    QString id() const override { return QStringLiteral("tesseract"); }
    QString displayName() const override { return QStringLiteral("Tesseract（本地）"); }
    QString info() const override { return lang_; }

    bool load(const OcrConfig& config, QString* error) override;
    bool isLoaded() const override { return api_ != nullptr; }
    void setMinScore(float score) override {
        minScore_ = score < 0 ? 0 : (score > 1 ? 1 : score);
    }

    QVector<OcrLine> recognize(const cv::Mat& bgr, QString* error) override;

    static QStringList availableLanguages();

private:
    TessBaseAPI* api_ = nullptr;
    QString lang_;
    float minScore_ = 0.35f;
};
