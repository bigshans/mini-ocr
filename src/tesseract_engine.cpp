#include "tesseract_engine.h"

#include <tesseract/capi.h>

#include <QHash>
#include <QStringList>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <climits>

namespace {

QString joinWords(const QStringList& words) {
    QString out;
    for (const QString& word : words) {
        if (out.isEmpty()) {
            out = word;
            continue;
        }
        const QChar prev = out.back();
        const QChar next = word.front();
        const bool prevAscii = prev.unicode() < 128 &&
                               (prev.isLetterOrNumber() || prev == QLatin1Char('_'));
        const bool nextAscii = next.unicode() < 128 &&
                               (next.isLetterOrNumber() || next == QLatin1Char('_'));
        if (prevAscii && nextAscii) out += QLatin1Char(' ');
        out += word;
    }
    return out;
}

struct LineAccum {
    QStringList words;
    int x0 = INT_MAX;
    int y0 = INT_MAX;
    int x1 = 0;
    int y1 = 0;
    double confidence = 0.0;
    int count = 0;
};

}  // namespace

TesseractEngine::~TesseractEngine() {
    if (api_) TessBaseAPIDelete(api_);
}

QStringList TesseractEngine::availableLanguages() {
    QStringList result;
    TessBaseAPI* api = TessBaseAPICreate();
    if (!api) return result;
    char** languages = TessBaseAPIGetAvailableLanguagesAsVector(api);
    if (languages) {
        for (char** p = languages; *p != nullptr; ++p) {
            result << QString::fromUtf8(*p);
        }
        TessDeleteTextArray(languages);
    }
    TessBaseAPIDelete(api);
    result.sort();
    return result;
}

bool TesseractEngine::load(const OcrConfig& config, QString* error) {
    if (api_) {
        TessBaseAPIDelete(api_);
        api_ = nullptr;
    }

    lang_ = config.tesseractLang.trimmed();
    if (lang_.isEmpty()) lang_ = QStringLiteral("chi_sim+eng");

    api_ = TessBaseAPICreate();
    if (!api_) {
        if (error) *error = QStringLiteral("无法创建 Tesseract 实例");
        return false;
    }

    if (TessBaseAPIInit3(api_, nullptr, lang_.toUtf8().constData()) != 0) {
        if (error) {
            *error = QStringLiteral("Tesseract 初始化失败，语言：%1。请确认已安装对应的 "
                                    "tesseract-data 语言包。")
                         .arg(lang_);
        }
        TessBaseAPIDelete(api_);
        api_ = nullptr;
        return false;
    }

    TessBaseAPISetPageSegMode(api_, tesseract::PSM_AUTO);
    return true;
}

QVector<OcrLine> TesseractEngine::recognize(const cv::Mat& bgr, QString* error) {
    QVector<OcrLine> lines;
    if (!api_) {
        if (error) *error = QStringLiteral("Tesseract 未加载");
        return lines;
    }
    if (bgr.empty()) return lines;

    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    TessBaseAPISetImage(api_, rgb.data, rgb.cols, rgb.rows, 3,
                        static_cast<int>(rgb.step));
    TessBaseAPISetSourceResolution(api_, 300);

    if (TessBaseAPIRecognize(api_, nullptr) != 0) {
        TessBaseAPIClear(api_);
        if (error) *error = QStringLiteral("Tesseract 识别失败");
        return lines;
    }

    char* tsvRaw = TessBaseAPIGetTsvText(api_, 0);
    const QString tsv = tsvRaw ? QString::fromUtf8(tsvRaw) : QString();
    if (tsvRaw) TessDeleteText(tsvRaw);

    QVector<LineAccum> accum;
    QHash<QString, int> index;

    const QStringList rows = tsv.split(QLatin1Char('\n'));
    for (const QString& row : rows) {
        if (row.isEmpty()) continue;
        const QStringList fields = row.split(QLatin1Char('\t'), Qt::KeepEmptyParts);
        if (fields.size() < 12) continue;
        if (fields[0].trimmed() != QStringLiteral("5")) continue;  // word level

        const QString word = fields.mid(11).join(QLatin1Char('\t')).trimmed();
        if (word.isEmpty()) continue;

        const double confidence = fields[10].toDouble();
        if (confidence < 0) continue;

        const int left = fields[6].toInt();
        const int top = fields[7].toInt();
        const int width = fields[8].toInt();
        const int height = fields[9].toInt();
        const QString key = fields[2] + QLatin1Char('/') + fields[3] +
                            QLatin1Char('/') + fields[4];

        int idx = index.value(key, -1);
        if (idx < 0) {
            idx = accum.size();
            index.insert(key, idx);
            accum.push_back(LineAccum{});
        }
        LineAccum& line = accum[idx];
        line.words << word;
        line.x0 = std::min(line.x0, left);
        line.y0 = std::min(line.y0, top);
        line.x1 = std::max(line.x1, left + width);
        line.y1 = std::max(line.y1, top + height);
        line.confidence += confidence;
        ++line.count;
    }

    TessBaseAPIClear(api_);

    for (const LineAccum& line : accum) {
        const QString text = joinWords(line.words);
        if (text.isEmpty() || line.count == 0) continue;

        const float score = static_cast<float>(line.confidence / line.count / 100.0);
        if (score < minScore_) continue;

        lines.push_back({QRect(line.x0, line.y0,
                               std::max(1, line.x1 - line.x0),
                               std::max(1, line.y1 - line.y0)),
                         text, score});
    }

    std::sort(lines.begin(), lines.end(), [](const OcrLine& a, const OcrLine& b) {
        const int tolerance = std::max(8, a.box.height() / 2);
        if (std::abs(a.box.y() - b.box.y()) > tolerance) return a.box.y() < b.box.y();
        return a.box.x() < b.box.x();
    });

    return lines;
}
