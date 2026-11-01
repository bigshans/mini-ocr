#include "translate.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <functional>

namespace {

struct HttpResponse {
    QByteArray body;
    int status = 0;
    QString error;
};

HttpResponse httpCall(const QUrl& url, const QByteArray& method,
                      const QList<QPair<QByteArray, QByteArray>>& headers,
                      const QByteArray& body, int timeoutSec) {
    HttpResponse out;
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                                     "(KHTML, like Gecko) Chrome/124.0 Safari/537.36"));
    request.setRawHeader("Accept", "application/json, text/plain, */*");
    for (const auto& header : headers) request.setRawHeader(header.first, header.second);

    QNetworkReply* reply = method == QByteArrayLiteral("POST")
                               ? manager.post(request, body)
                               : manager.get(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(std::clamp(timeoutSec, 5, 180) * 1000);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        out.error = QStringLiteral("请求超时");
    } else {
        out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        out.body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && out.status == 0) {
            out.error = reply->errorString();
        }
    }
    reply->deleteLater();
    return out;
}

QString decodeEntities(QString text) {
    // MyMemory occasionally returns HTML entities such as &quot; or &#39;.
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));

    static const QRegularExpression numeric(
        QStringLiteral("&#(x?[0-9A-Fa-f]+);"),
        QRegularExpression::CaseInsensitiveOption);
    QString out;
    int last = 0;
    auto it = numeric.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        out += text.mid(last, match.capturedStart() - last);
        const QString code = match.captured(1);
        bool ok = false;
        const uint value = code.startsWith(QLatin1Char('x'), Qt::CaseInsensitive)
                               ? code.mid(1).toUInt(&ok, 16)
                               : code.toUInt(&ok, 10);
        if (ok && value > 0 && value <= 0x10FFFF) {
            if (value <= 0xFFFF) {
                out += QChar(static_cast<ushort>(value));
            } else {
                const char32_t cp = static_cast<char32_t>(value);
                out += QString::fromUcs4(&cp, 1);
            }
        } else {
            out += match.captured(0);
        }
        last = match.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

QString cleanLlmText(QString text) {
    text.remove(QRegularExpression(QStringLiteral(R"(<\|[^|]*\|>)")));
    text = text.trimmed();
    if (text.startsWith(QStringLiteral("```"))) {
        const int firstNewline = text.indexOf(QLatin1Char('\n'));
        if (firstNewline >= 0) text = text.mid(firstNewline + 1);
        if (text.endsWith(QStringLiteral("```"))) text.chop(3);
        text = text.trimmed();
    }
    return text;
}

// Split long text into provider-sized chunks, preferring line boundaries.
QStringList splitForTranslation(const QString& text, int maxChars) {
    QStringList chunks;
    QString current;
    auto flush = [&] {
        if (!current.isEmpty()) {
            chunks << current;
            current.clear();
        }
    };

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (line.size() > maxChars) {
            flush();
            for (int i = 0; i < line.size(); i += maxChars) {
                chunks << line.mid(i, maxChars);
            }
            continue;
        }
        const int extra = current.isEmpty() ? line.size() : line.size() + 1;
        if (current.size() + extra > maxChars) flush();
        if (!current.isEmpty()) current += QLatin1Char('\n');
        current += line;
    }
    flush();
    if (chunks.isEmpty()) chunks << text;
    return chunks;
}

QString libreCode(const QString& code) {
    if (code == QStringLiteral("zh-CN")) return QStringLiteral("zh");
    if (code == QStringLiteral("zh-TW")) return QStringLiteral("zt");
    return code;
}

QString endpointFor(const QString& baseUrl, const QString& suffix) {
    QString base = baseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    if (base.isEmpty()) base = QStringLiteral("https://api.deepseek.com");
    return base + suffix;
}

TranslationResult translateGoogle(const QString& text, const TranslationConfig& config) {
    TranslationResult result;
    result.engine = QStringLiteral("google");

    QStringList translated;
    for (const QString& chunk : splitForTranslation(text, 3000)) {
        QUrl url(QStringLiteral("https://clients5.google.com/translate_a/t"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("client"), QStringLiteral("dict-chrome-ex"));
        query.addQueryItem(QStringLiteral("sl"),
                           config.sourceLang.trimmed().isEmpty() ? QStringLiteral("auto")
                                                                 : config.sourceLang);
        query.addQueryItem(QStringLiteral("tl"), config.targetLang);
        url.setQuery(query);

        const QByteArray body =
            "q=" + QUrl::toPercentEncoding(chunk);
        const HttpResponse response =
            httpCall(url, QByteArrayLiteral("POST"),
                     {{"Content-Type", "application/x-www-form-urlencoded"}}, body,
                     config.timeoutSec);

        if (!response.error.isEmpty()) {
            result.error = response.error;
            return result;
        }
        const QByteArray trimmed = response.body.trimmed();
        if (response.status != 200 || trimmed.startsWith('<')) {
            result.error = QStringLiteral("Google 免费接口返回异常（可能被限流），"
                                          "请稍后重试或切换翻译引擎");
            return result;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(response.body);
        if (!doc.isArray()) {
            result.error = QStringLiteral("Google 返回格式无法解析");
            return result;
        }

        QStringList parts;
        std::function<void(const QJsonValue&)> collect = [&](const QJsonValue& value) {
            if (value.isString()) {
                parts << value.toString();
                return;
            }
            if (!value.isArray()) return;
            const QJsonArray array = value.toArray();
            if (array.size() >= 2 && array.at(0).isString() && array.at(1).isString()) {
                parts << array.at(0).toString();
                return;
            }
            for (const QJsonValue& item : array) collect(item);
        };
        collect(doc.array().isEmpty() ? QJsonValue() : QJsonValue(doc.array()));
        if (parts.isEmpty()) {
            result.error = QStringLiteral("Google 未返回译文");
            return result;
        }
        translated << parts.join(QLatin1Char('\n'));
    }

    result.text = translated.join(QLatin1Char('\n'));
    if (result.text.trimmed().isEmpty()) result.error = QStringLiteral("Google 未返回译文");
    return result;
}

TranslationResult translateMyMemory(const QString& text, const TranslationConfig& config) {
    TranslationResult result;
    result.engine = QStringLiteral("mymemory");

    QString source = config.sourceLang;
    if (source.trimmed().isEmpty() || source == QStringLiteral("auto")) {
        source = guessSourceLanguage(text);
    }
    if (source == config.targetLang) {
        result.text = text;
        return result;
    }

    QStringList translated;
    // MyMemory caps each query at roughly 500 characters.
    for (const QString& chunk : splitForTranslation(text, 400)) {
        QUrl url(QStringLiteral("https://api.mymemory.translated.net/get"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("q"), chunk);
        query.addQueryItem(QStringLiteral("langpair"),
                           source + QLatin1Char('|') + config.targetLang);
        url.setQuery(query);

        const HttpResponse response = httpCall(url, QByteArrayLiteral("GET"), {}, {},
                                               config.timeoutSec);
        if (!response.error.isEmpty()) {
            result.error = response.error;
            return result;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(response.body);
        if (!doc.isObject()) {
            result.error = QStringLiteral("MyMemory 返回格式无法解析");
            return result;
        }
        const QJsonObject root = doc.object();
        const QJsonObject data = root.value(QStringLiteral("responseData")).toObject();
        QString piece = decodeEntities(data.value(QStringLiteral("translatedText")).toString());
        if (piece.trimmed().isEmpty()) {
            const QString details = root.value(QStringLiteral("responseDetails")).toString();
            result.error = details.isEmpty() ? QStringLiteral("MyMemory 未返回译文")
                                             : details;
            return result;
        }
        if (piece.contains(QStringLiteral("INVALID SOURCE LANGUAGE")) ||
            piece.contains(QStringLiteral("QUERY LENGTH LIMIT"))) {
            result.error = piece;
            return result;
        }
        translated << piece;
    }

    result.text = translated.join(QLatin1Char('\n'));
    if (result.text.trimmed().isEmpty()) result.error = QStringLiteral("MyMemory 未返回译文");
    return result;
}

TranslationResult translateLibre(const QString& text, const TranslationConfig& config) {
    TranslationResult result;
    result.engine = QStringLiteral("libretranslate");

    QString source = config.sourceLang.trimmed();
    if (source.isEmpty()) source = QStringLiteral("auto");
    if (source != QStringLiteral("auto")) source = libreCode(source);

    QStringList translated;
    for (const QString& chunk : splitForTranslation(text, 4000)) {
        QJsonObject payload;
        payload.insert(QStringLiteral("q"), chunk);
        payload.insert(QStringLiteral("source"), source);
        payload.insert(QStringLiteral("target"), libreCode(config.targetLang));
        payload.insert(QStringLiteral("format"), QStringLiteral("text"));
        if (!config.libreApiKey.isEmpty()) {
            payload.insert(QStringLiteral("api_key"), config.libreApiKey);
        }

        const HttpResponse response =
            httpCall(QUrl(endpointFor(config.libreUrl, QStringLiteral("/translate"))),
                     QByteArrayLiteral("POST"),
                     {{"Content-Type", "application/json"}},
                     QJsonDocument(payload).toJson(QJsonDocument::Compact),
                     config.timeoutSec);
        if (!response.error.isEmpty()) {
            result.error = response.error;
            return result;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(response.body);
        if (!doc.isObject()) {
            result.error = QStringLiteral("LibreTranslate 返回格式无法解析");
            return result;
        }
        const QJsonObject root = doc.object();
        if (root.contains(QStringLiteral("error"))) {
            result.error = root.value(QStringLiteral("error")).toString();
            return result;
        }
        const QString piece = root.value(QStringLiteral("translatedText")).toString();
        if (piece.trimmed().isEmpty()) {
            result.error = QStringLiteral("LibreTranslate 未返回译文");
            return result;
        }
        translated << piece;
    }

    result.text = translated.join(QLatin1Char('\n'));
    if (result.text.trimmed().isEmpty()) {
        result.error = QStringLiteral("LibreTranslate 未返回译文");
    }
    return result;
}

TranslationResult translateDeepSeek(const QString& text, const TranslationConfig& config) {
    TranslationResult result;
    result.engine = QStringLiteral("deepseek");

    if (config.cloudApiKey.trimmed().isEmpty()) {
        result.error = QStringLiteral("DeepSeek 翻译需要 API Key（与云端 OCR 共用）");
        return result;
    }

    const QString targetName = translationLanguageName(config.targetLang);
    const QString system =
        QStringLiteral("你是一个只输出译文的翻译引擎。把用户提供的文本翻译成%1。"
                       "保留换行、列表、标点和原有格式；只输出译文，不要解释，不要附原文。")
            .arg(targetName);

    QString model = config.cloudModel.trimmed();
    if (config.cloudBaseUrl.contains(QStringLiteral("api.deepseek.com"))) {
        // The official vision model (deepseek-flash) is for OCR; deepseek-chat is
        // the better and cheaper choice for text translation.
        model = QStringLiteral("deepseek-chat");
    } else {
        if (model.isEmpty()) model = QStringLiteral("deepseek-chat");
        if (model.contains(QStringLiteral("OCR"), Qt::CaseInsensitive)) {
            result.error = QStringLiteral(
                "当前云端模型是 OCR 专用模型，不能用于翻译；请在设置里改用 DeepSeek "
                "官方地址，或填写一个文本模型（例如 deepseek-chat、Qwen）。");
            return result;
        }
    }

    QStringList translated;
    for (const QString& chunk : splitForTranslation(text, 3000)) {
        QJsonArray messages;
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                    {QStringLiteral("content"), system}});
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                    {QStringLiteral("content"), chunk}});

        const int maxTokens =
            std::clamp(static_cast<int>(chunk.size() * 2 + 512), 512, 8192);
        QJsonObject payload;
        payload.insert(QStringLiteral("model"), model);
        payload.insert(QStringLiteral("messages"), messages);
        payload.insert(QStringLiteral("temperature"), 0);
        payload.insert(QStringLiteral("max_tokens"), maxTokens);
        payload.insert(QStringLiteral("stream"), false);

        const HttpResponse response =
            httpCall(QUrl(endpointFor(config.cloudBaseUrl,
                                      QStringLiteral("/chat/completions"))),
                     QByteArrayLiteral("POST"),
                     {{"Content-Type", "application/json"},
                      {"Authorization", "Bearer " + config.cloudApiKey.toUtf8()}},
                     QJsonDocument(payload).toJson(QJsonDocument::Compact),
                     config.timeoutSec);
        if (!response.error.isEmpty()) {
            result.error = response.error;
            return result;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(response.body);
        if (!doc.isObject()) {
            result.error = QStringLiteral("DeepSeek 返回格式无法解析");
            return result;
        }
        const QJsonObject root = doc.object();
        if (root.contains(QStringLiteral("error"))) {
            const QJsonObject error = root.value(QStringLiteral("error")).toObject();
            result.error = error.value(QStringLiteral("message")).toString();
            if (result.error.isEmpty()) result.error = QStringLiteral("DeepSeek 请求失败");
            return result;
        }
        const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            result.error = QStringLiteral("DeepSeek 未返回译文");
            return result;
        }
        const QString piece = cleanLlmText(
            choices.first().toObject().value(QStringLiteral("message")).toObject()
                .value(QStringLiteral("content")).toString());
        if (piece.isEmpty()) {
            result.error = QStringLiteral("DeepSeek 未返回译文");
            return result;
        }
        translated << piece;
    }

    result.text = translated.join(QLatin1Char('\n'));
    if (result.text.trimmed().isEmpty()) result.error = QStringLiteral("DeepSeek 未返回译文");
    return result;
}

}  // namespace

QVector<QPair<QString, QString>> translationLanguages() {
    static const QVector<QPair<QString, QString>> languages = {
        {QStringLiteral("zh-CN"), QStringLiteral("简体中文")},
        {QStringLiteral("zh-TW"), QStringLiteral("繁體中文")},
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("ja"), QStringLiteral("日本語")},
        {QStringLiteral("ko"), QStringLiteral("한국어")},
        {QStringLiteral("fr"), QStringLiteral("Français")},
        {QStringLiteral("de"), QStringLiteral("Deutsch")},
        {QStringLiteral("es"), QStringLiteral("Español")},
        {QStringLiteral("ru"), QStringLiteral("Русский")},
        {QStringLiteral("pt"), QStringLiteral("Português")},
        {QStringLiteral("it"), QStringLiteral("Italiano")},
        {QStringLiteral("ar"), QStringLiteral("العربية")},
    };
    return languages;
}

QString translationLanguageName(const QString& code) {
    for (const auto& language : translationLanguages()) {
        if (language.first == code) return language.second;
    }
    return code;
}

QString translationEngineName(const QString& id) {
    if (id == QStringLiteral("mymemory")) return QStringLiteral("MyMemory");
    if (id == QStringLiteral("libretranslate")) return QStringLiteral("LibreTranslate");
    if (id == QStringLiteral("deepseek")) return QStringLiteral("DeepSeek");
    return QStringLiteral("Google 免费");
}

QString guessSourceLanguage(const QString& text) {
    int cjk = 0, kana = 0, hangul = 0, cyrillic = 0, arabic = 0, latin = 0;
    for (const QChar ch : text) {
        const ushort u = ch.unicode();
        if (u >= 0x3040 && u <= 0x30FF) {
            ++kana;
        } else if (u >= 0xAC00 && u <= 0xD7AF) {
            ++hangul;
        } else if (u >= 0x4E00 && u <= 0x9FFF) {
            ++cjk;
        } else if (u >= 0x0400 && u <= 0x04FF) {
            ++cyrillic;
        } else if (u >= 0x0600 && u <= 0x06FF) {
            ++arabic;
        } else if (ch.isLetter()) {
            ++latin;
        }
    }
    if (kana > 0) return QStringLiteral("ja");
    if (hangul > 0) return QStringLiteral("ko");
    if (cjk > 0 && cjk >= latin) return QStringLiteral("zh-CN");
    if (cyrillic > latin) return QStringLiteral("ru");
    if (arabic > latin) return QStringLiteral("ar");
    return QStringLiteral("en");
}

TranslationResult translateText(const QString& text, const TranslationConfig& config) {
    TranslationResult result;
    if (text.trimmed().isEmpty()) {
        result.error = QStringLiteral("没有可翻译的文字");
        return result;
    }

    const QString engine = config.engine.trimmed().isEmpty() ? QStringLiteral("google")
                                                             : config.engine.trimmed();
    if (engine == QStringLiteral("mymemory")) {
        result = translateMyMemory(text, config);
    } else if (engine == QStringLiteral("libretranslate")) {
        result = translateLibre(text, config);
    } else if (engine == QStringLiteral("deepseek")) {
        result = translateDeepSeek(text, config);
    } else {
        result = translateGoogle(text, config);
        // The keyless Google endpoint is unofficial and occasionally rate-limited.
        if (!result.error.isEmpty()) {
            TranslationResult fallback = translateMyMemory(text, config);
            if (fallback.error.isEmpty()) {
                fallback.engine = QStringLiteral("MyMemory（备用）");
                return fallback;
            }
            result.error += QStringLiteral("；备用 MyMemory 也失败：") + fallback.error;
        }
    }
    return result;
}
