#pragma once

#include "ocr_backend.h"

#include <QImage>
#include <QString>

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>

#include <string>
#include <vector>

class OcrEngine : public IOcrBackend {
public:
    QString id() const override { return QStringLiteral("ppocr"); }
    QString displayName() const override { return QStringLiteral("PP-OCRv5（本地）"); }
    QString info() const override { return modelDir_; }

    bool load(const OcrConfig& config, QString* error) override;
    bool isLoaded() const override { return loaded_; }
    void setMinScore(float score) override {
        minScore_ = score < 0 ? 0 : (score > 1 ? 1 : score);
    }

    QVector<OcrLine> recognize(const cv::Mat& bgr, QString* error) override;

    static cv::Mat qImageToBgr(const QImage& image);

private:
    bool loaded_ = false;
    QString modelDir_;
    cv::dnn::Net det_;
    cv::dnn::Net rec_;
    std::vector<std::string> dict_;
    float minScore_ = 0.35f;
};
