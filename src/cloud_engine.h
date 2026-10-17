#pragma once

#include "ocr_backend.h"

#include <QString>

class CloudOcrEngine : public IOcrBackend {
public:
    QString id() const override { return QStringLiteral("cloud"); }
    QString displayName() const override { return QStringLiteral("DeepSeek（云端）"); }
    QString info() const override;

    bool load(const OcrConfig& config, QString* error) override;
    bool isLoaded() const override { return loaded_; }
    void setMinScore(float score) override {
        minScore_ = score < 0 ? 0 : (score > 1 ? 1 : score);
    }

    QVector<OcrLine> recognize(const cv::Mat& bgr, QString* error) override;

private:
    QString baseUrl_;
    QString model_;
    QString apiKey_;
    QString prompt_;
    int timeoutSec_ = 60;
    bool loaded_ = false;
    float minScore_ = 0.35f;
};
