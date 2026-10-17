#pragma once

#include <QPair>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace cv {
class Mat;
}

struct OcrLine {
    QRect box;
    QString text;
    float score = 0.0f;
};

struct OcrConfig {
    QString modelDir;                         // PP-OCR model directory
    QString tesseractLang = QStringLiteral("chi_sim+eng");

    // OpenAI-compatible cloud OCR (DeepSeek-OCR via SiliconFlow by default).
    QString cloudBaseUrl = QStringLiteral("https://api.deepseek.com");
    QString cloudModel = QStringLiteral("deepseek-flash");
    QString cloudApiKey;
    QString cloudPrompt = QStringLiteral("请识别图片中的所有文字，按原有行顺序输出，只输出文字，不要解释。");
    int cloudTimeoutSec = 60;
};

class IOcrBackend {
public:
    virtual ~IOcrBackend() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QString info() const = 0;

    virtual bool load(const OcrConfig& config, QString* error) = 0;
    virtual bool isLoaded() const = 0;
    virtual void setMinScore(float score) = 0;

    virtual QVector<OcrLine> recognize(const cv::Mat& bgr, QString* error) = 0;
};

std::unique_ptr<IOcrBackend> createOcrBackend(const QString& id);
QVector<QPair<QString, QString>> availableOcrBackends();
QStringList availableTesseractLanguages();
