#include "cloud_engine.h"

#include <QBuffer>
#include <QEventLoop>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <opencv2/imgproc.hpp>

#include <algorithm>

namespace {

QString jsonError(const QJsonObject& root) {
    const QJsonValue error = root.value(QStringLiteral("error"));
    if (error.isObject()) {
        const QJsonObject obj = error.toObject();
        QString message = obj.value(QStringLiteral("message")).toString();
        if (message.isEmpty()) message = obj.value(QStringLiteral("code")).toString();
        return message;
    }
    if (error.isString()) return error.toString();
    return {};
}

QString cleanCloudText(QString text) {
    text.remove(QRegularExpression(QStringLiteral(R"(<\|[^|]*\|>)")));
    text.remove(QStringLiteral("<image>"));
    text = text.trimmed();

    if (text.startsWith(QStringLiteral("```"))) {
        const int firstNewline = text.indexOf(QLatin1Char('\n'));
        if (firstNewline >= 0) text = text.mid(firstNewline + 1);
        if (text.endsWith(QStringLiteral("```"))) text.chop(3);
        text = text.trimmed();
    }
    return text;
}

}  // namespace

QString CloudOcrEngine::info() const {
    return QStringLiteral("%1 @ %2").arg(model_, baseUrl_);
}

bool CloudOcrEngine::load(const OcrConfig& config, QString* error) {
    loaded_ = false;
    baseUrl_ = config.cloudBaseUrl.trimmed();
    if (baseUrl_.isEmpty()) baseUrl_ = QStringLiteral("https://api.deepseek.com");
    while (baseUrl_.endsWith(QLatin1Char('/'))) baseUrl_.chop(1);

    model_ = config.cloudModel.trimmed();
    if (model_.isEmpty()) model_ = QStringLiteral("deepseek-flash");

    apiKey_ = config.cloudApiKey.trimmed();
    if (apiKey_.isEmpty()) {
        if (error) {
            *error = QStringLiteral("云端 OCR 未配置 API Key。请在设置 → 识别引擎 → "
                                    "DeepSeek OCR 中填写。");
        }
        return false;
    }

    prompt_ = config.cloudPrompt.trimmed();
    if (prompt_.isEmpty()) {
        prompt_ = QStringLiteral("请识别图片中的所有文字，按原有行顺序输出，只输出文字，不要解释。");
    }
    timeoutSec_ = std::clamp(config.cloudTimeoutSec, 10, 300);
    loaded_ = true;
    return true;
}

QVector<OcrLine> CloudOcrEngine::recognize(const cv::Mat& bgr, QString* error) {
    QVector<OcrLine> lines;
    if (!loaded_) {
        if (error) *error = QStringLiteral("云端 OCR 未加载");
        return lines;
    }
    if (bgr.empty()) return lines;

    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    QImage image(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
                 QImage::Format_RGB888);
    QByteArray png;
    {
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) {
            if (error) *error = QStringLiteral("图片编码失败");
            return lines;
        }
    }

    const QString dataUrl =
        QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());

    QJsonObject imageUrl;
    imageUrl.insert(QStringLiteral("url"), dataUrl);
    imageUrl.insert(QStringLiteral("detail"), QStringLiteral("high"));

    QJsonObject imagePart;
    imagePart.insert(QStringLiteral("type"), QStringLiteral("image_url"));
    imagePart.insert(QStringLiteral("image_url"), imageUrl);

    QJsonObject textPart;
    textPart.insert(QStringLiteral("type"), QStringLiteral("text"));
    textPart.insert(QStringLiteral("text"), prompt_);

    QJsonArray content;
    content.append(imagePart);
    content.append(textPart);

    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("user"));
    message.insert(QStringLiteral("content"), content);

    QJsonArray messages;
    messages.append(message);

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), model_);
    payload.insert(QStringLiteral("messages"), messages);
    payload.insert(QStringLiteral("temperature"), 0);
    payload.insert(QStringLiteral("max_tokens"), 4096);
    payload.insert(QStringLiteral("stream"), false);

    QString endpoint = baseUrl_;
    if (!endpoint.endsWith(QStringLiteral("/chat/completions"))) {
        endpoint += QStringLiteral("/chat/completions");
    }

    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + apiKey_.toUtf8());

    QNetworkAccessManager manager;
    QNetworkReply* reply =
        manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    bool timedOut = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, reply, [&] {
        timedOut = true;
        reply->abort();
    });
    timer.start(timeoutSec_ * 1000);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    timer.stop();

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();

    if (timedOut) {
        if (error) *error = QStringLiteral("云端 OCR 请求超时（%1 秒）").arg(timeoutSec_);
        return lines;
    }
    if (networkError != QNetworkReply::NoError) {
        if (error) {
            *error = QStringLiteral("云端 OCR 网络错误：%1").arg(networkErrorText);
        }
        return lines;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = QStringLiteral("云端返回内容不是有效 JSON（HTTP %1）：%2")
                         .arg(status)
                         .arg(QString::fromUtf8(body.left(300)));
        }
        return lines;
    }

    const QJsonObject root = doc.object();
    const QString serviceError = jsonError(root);
    if (status < 200 || status >= 300 || !serviceError.isEmpty()) {
        if (error) {
            *error = QStringLiteral("云端 OCR 失败（HTTP %1）：%2")
                         .arg(status)
                         .arg(serviceError.isEmpty()
                                  ? QString::fromUtf8(body.left(400))
                                  : serviceError);
        }
        return lines;
    }

    QString text;
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (!choices.isEmpty()) {
        const QJsonObject choice = choices.first().toObject();
        const QJsonObject messageObj = choice.value(QStringLiteral("message")).toObject();
        const QJsonValue contentValue = messageObj.value(QStringLiteral("content"));
        if (contentValue.isString()) {
            text = contentValue.toString();
        } else if (contentValue.isArray()) {
            for (const QJsonValue& part : contentValue.toArray()) {
                const QJsonObject obj = part.toObject();
                if (obj.value(QStringLiteral("type")).toString() ==
                    QStringLiteral("text")) {
                    text += obj.value(QStringLiteral("text")).toString();
                }
            }
        }
        if (text.isEmpty()) {
            text = choice.value(QStringLiteral("text")).toString();
        }
    }

    text = cleanCloudText(text);
    if (text.isEmpty()) {
        if (error) *error = QStringLiteral("云端没有返回文字");
        return lines;
    }

    const QStringList textLines = text.split(QLatin1Char('\n'));
    for (const QString& raw : textLines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        if (minScore_ > 1.0f) continue;
        lines.push_back({QRect(), line, 1.0f});
    }
    return lines;
}
