#include "ocr_engine.h"

#include <QFileInfo>
#include <QTextStream>
#include <QElapsedTimer>

#include <opencv2/core/version.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#if CV_VERSION_MAJOR >= 5
#include <opencv2/geometry/2d.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <fstream>

namespace {

constexpr float kDetThreshold = 0.30f;
constexpr float kBoxThreshold = 0.50f;
constexpr int kDetMaxSide = 960;
constexpr int kRecHeight = 48;
constexpr int kRecMaxWidth = 4096;

std::vector<std::string> readDict(const QString& path, QString* error) {
    std::ifstream f(path.toStdString(), std::ios::binary);
    if (!f) {
        if (error) *error = QStringLiteral("无法读取字典文件: %1").arg(path);
        return {};
    }
    std::vector<std::string> dict;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        dict.push_back(line);
    }
    return dict;
}

cv::Mat makeBlob(const cv::Mat& bgr, int w, int h, const float mean[3], const float stdv[3]) {
    cv::Mat blob;
    cv::dnn::blobFromImage(bgr, blob, 1.0 / 255.0, cv::Size(w, h),
                          cv::Scalar(0, 0, 0), false, false, CV_32F);
    float* p = blob.ptr<float>();
    const int plane = w * h;
    for (int c = 0; c < 3; ++c) {
        for (int i = 0; i < plane; ++i) {
            p[c * plane + i] = (p[c * plane + i] - mean[c]) / stdv[c];
        }
    }
    return blob;
}

QString ctcDecode(const cv::Mat& out, const std::vector<std::string>& dict, float* score) {
    if (out.dims != 3 || out.size[0] != 1) return {};
    const int T = out.size[1];
    const int C = out.size[2];
    const float* p = out.ptr<float>();
    QString text;
    int prev = -1;
    double sum = 0.0;
    int n = 0;

    for (int t = 0; t < T; ++t) {
        int best = 0;
        float bv = p[t * C];
        for (int c = 1; c < C; ++c) {
            const float v = p[t * C + c];
            if (v > bv) { bv = v; best = c; }
        }
        if (best != prev) {
            if (best > 0) {
                if (best - 1 < static_cast<int>(dict.size())) {
                    text += QString::fromUtf8(dict[best - 1].c_str());
                } else {
                    text += QLatin1Char(' ');  // PaddleOCR use_space_char
                }
                sum += bv;
                ++n;
            }
        }
        prev = best;
    }
    if (score) *score = n ? static_cast<float>(sum / n) : 0.0f;
    return text;
}

}  // namespace

cv::Mat OcrEngine::qImageToBgr(const QImage& image) {
    QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    cv::Mat view(rgb.height(), rgb.width(), CV_8UC3,
                 const_cast<uchar*>(rgb.constBits()),
                 static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(view, bgr, cv::COLOR_RGB2BGR);
    return bgr;
}

bool OcrEngine::load(const OcrConfig& config, QString* error) {
    loaded_ = false;
    dict_.clear();
    const QString modelDir = config.modelDir;
    modelDir_ = modelDir;
    const QString detPath = modelDir + QStringLiteral("/ch_PP-OCRv5_det_mobile.onnx");
    const QString recPath = modelDir + QStringLiteral("/ch_PP-OCRv5_rec_mobile.onnx");
    const QString dictPath = modelDir + QStringLiteral("/ppocrv5_dict.txt");

    for (const QString& p : {detPath, recPath, dictPath}) {
        if (!QFileInfo::exists(p)) {
            if (error) *error = QStringLiteral("模型文件不存在: %1").arg(p);
            return false;
        }
    }

    try {
        dict_ = readDict(dictPath, error);
        if (dict_.empty()) return false;
        det_ = cv::dnn::readNet(detPath.toStdString());
        rec_ = cv::dnn::readNet(recPath.toStdString());
    } catch (const cv::Exception& e) {
        if (error) *error = QString::fromStdString(e.what());
        return false;
    }

    loaded_ = true;
    return true;
}

QVector<OcrLine> OcrEngine::recognize(const cv::Mat& img, QString* error) {
    QVector<OcrLine> lines;
    if (!loaded_ || img.empty()) {
        if (error) *error = QStringLiteral("OCR 引擎未加载或图片为空");
        return lines;
    }

    try {
        // --- detection: resize longest side to <= 960, keep aspect, pad to /32 ---
        const float scale = std::min(1.0f, static_cast<float>(kDetMaxSide) /
                                              std::max(img.cols, img.rows));
        const int nw = std::max(32, static_cast<int>(std::round(img.cols * scale / 32)) * 32);
        const int nh = std::max(32, static_cast<int>(std::round(img.rows * scale / 32)) * 32);

        cv::Mat resized;
        cv::resize(img, resized, cv::Size(nw, nh));
        const float detMean[3] = {0.485f, 0.456f, 0.406f};
        const float detStd[3] = {0.229f, 0.224f, 0.225f};
        det_.setInput(makeBlob(resized, nw, nh, detMean, detStd));
        cv::Mat pred = det_.forward();
        if (pred.dims != 4) {
            if (error) *error = QStringLiteral("检测模型输出维度异常");
            return lines;
        }
        cv::Mat prob = pred.reshape(1, nh);  // [nh, nw]

        cv::Mat bitmap = prob > kDetThreshold;
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(bitmap, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

        for (const auto& contour : contours) {
            cv::Rect r = cv::boundingRect(contour);
            if (r.width < 4 || r.height < 4 || r.width * r.height < 30) continue;

            cv::Mat mask = cv::Mat::zeros(prob.size(), CV_8U);
            cv::drawContours(mask, std::vector<std::vector<cv::Point>>{contour},
                             -1, 255, cv::FILLED);
            const float regionScore = static_cast<float>(cv::mean(prob, mask)[0]);
            if (regionScore < kBoxThreshold) continue;

            // PaddleOCR unclip: expand the detected text region before cropping.
            const int padX = std::max(2, static_cast<int>(std::round(r.height * 0.5)));
            const int padY = std::max(2, static_cast<int>(std::round(r.height * 0.5)));
            const int x0 = std::max(0, static_cast<int>(std::floor((r.x - padX) / scale)));
            const int y0 = std::max(0, static_cast<int>(std::floor((r.y - padY) / scale)));
            const int x1 = std::min(img.cols, static_cast<int>(std::ceil((r.x + r.width + padX) / scale)));
            const int y1 = std::min(img.rows, static_cast<int>(std::ceil((r.y + r.height + padY) / scale)));
            if (x1 - x0 < 4 || y1 - y0 < 4) continue;

            cv::Mat crop = img(cv::Rect(x0, y0, x1 - x0, y1 - y0));
            int rw = std::max(16, static_cast<int>(std::round(crop.cols * 48.0 / crop.rows)));
            rw = std::min(rw, kRecMaxWidth);

            cv::Mat recInput;
            cv::resize(crop, recInput, cv::Size(rw, kRecHeight));
            const float recMean[3] = {0.5f, 0.5f, 0.5f};
            const float recStd[3] = {0.5f, 0.5f, 0.5f};
            rec_.setInput(makeBlob(recInput, rw, kRecHeight, recMean, recStd));
            cv::Mat out = rec_.forward();

            float score = 0.0f;
            QString text = ctcDecode(out, dict_, &score);
            if (text.isEmpty() || score < minScore_) continue;

            lines.push_back({QRect(x0, y0, x1 - x0, y1 - y0), text, score});
        }

        std::sort(lines.begin(), lines.end(), [](const OcrLine& a, const OcrLine& b) {
            const int tol = std::max(8, a.box.height() / 2);
            if (std::abs(a.box.y() - b.box.y()) > tol) return a.box.y() < b.box.y();
            return a.box.x() < b.box.x();
        });
    } catch (const cv::Exception& e) {
        if (error) *error = QString::fromStdString(e.what());
        lines.clear();
    }

    return lines;
}
