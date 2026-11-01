#pragma once

#include <QPair>
#include <QString>
#include <QVector>

struct TranslationConfig {
    QString engine = QStringLiteral("google");
    QString sourceLang = QStringLiteral("auto");
    QString targetLang = QStringLiteral("zh-CN");
    QString libreUrl = QStringLiteral("http://localhost:5000");
    QString libreApiKey;
    QString cloudBaseUrl = QStringLiteral("https://api.deepseek.com");
    QString cloudModel = QStringLiteral("deepseek-flash");
    QString cloudApiKey;
    int timeoutSec = 60;
};

struct TranslationResult {
    QString text;
    QString engine;  // engine that actually produced the result
    QString error;
};

// code -> display name, used by the settings and result windows.
QVector<QPair<QString, QString>> translationLanguages();
QString translationLanguageName(const QString& code);
QString translationEngineName(const QString& id);

// Crude language detection used by providers that do not support "auto".
QString guessSourceLanguage(const QString& text);

// Blocking call. Run it from a worker thread (QtConcurrent), not the GUI thread.
TranslationResult translateText(const QString& text, const TranslationConfig& config);
