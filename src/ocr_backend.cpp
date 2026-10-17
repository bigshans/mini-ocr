#include "ocr_backend.h"

#include "cloud_engine.h"
#include "ocr_engine.h"

#ifdef MINI_OCR_HAVE_TESSERACT
#include "tesseract_engine.h"
#endif


std::unique_ptr<IOcrBackend> createOcrBackend(const QString& id) {
    if (id == QStringLiteral("cloud")) {
        return std::make_unique<CloudOcrEngine>();
    }
    if (id == QStringLiteral("tesseract")) {
#ifdef MINI_OCR_HAVE_TESSERACT
        return std::make_unique<TesseractEngine>();
#else
        return nullptr;
#endif
    }
    return std::make_unique<OcrEngine>();
}

QVector<QPair<QString, QString>> availableOcrBackends() {
    QVector<QPair<QString, QString>> backends;
    backends.append({QStringLiteral("ppocr"), QStringLiteral("PP-OCRv5（本地）")});
#ifdef MINI_OCR_HAVE_TESSERACT
    backends.append({QStringLiteral("tesseract"), QStringLiteral("Tesseract（本地）")});
#endif
    backends.append({QStringLiteral("cloud"), QStringLiteral("DeepSeek（云端）")});
    return backends;
}

QStringList availableTesseractLanguages() {
#ifdef MINI_OCR_HAVE_TESSERACT
    return TesseractEngine::availableLanguages();
#else
    return {};
#endif
}
