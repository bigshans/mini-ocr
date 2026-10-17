#include "settings.h"

#include "ocr_backend.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QShowEvent>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace {

QString settingsPath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
           QStringLiteral("/mini-ocr.conf");
}

QString normalizedModelDir(const QString& dir) {
    const QString trimmed = dir.trimmed();
    return trimmed.isEmpty() ? QString() : QDir(trimmed).absolutePath();
}

bool looksLikeModelDir(const QString& dir) {
    return QFileInfo::exists(dir + QStringLiteral("/ch_PP-OCRv5_det_mobile.onnx")) &&
           QFileInfo::exists(dir + QStringLiteral("/ch_PP-OCRv5_rec_mobile.onnx")) &&
           QFileInfo::exists(dir + QStringLiteral("/ppocrv5_dict.txt"));
}

struct CloudPreset {
    QString id;
    QString label;
    QString baseUrl;
    QString model;
    QString prompt;
};

const QVector<CloudPreset>& cloudPresets() {
    static const QVector<CloudPreset> presets = {
        {QStringLiteral("deepseek-official"),
         QStringLiteral("DeepSeek 官方（deepseek-flash）"),
         QStringLiteral("https://api.deepseek.com"),
         QStringLiteral("deepseek-flash"),
         QStringLiteral("请识别图片中的所有文字，按原有行顺序输出，只输出文字，不要解释。")},
        {QStringLiteral("siliconflow-ocr"),
         QStringLiteral("SiliconFlow（DeepSeek-OCR，限免）"),
         QStringLiteral("https://api.siliconflow.cn/v1"),
         QStringLiteral("deepseek-ai/DeepSeek-OCR"),
         QStringLiteral("Free OCR.")},
        {QStringLiteral("custom"), QStringLiteral("自定义"), {}, {}, {}},
    };
    return presets;
}

QString inferCloudPreset(const QString& baseUrl, const QString& model) {
    if (baseUrl.contains(QStringLiteral("api.deepseek.com")) &&
        model == QStringLiteral("deepseek-flash")) {
        return QStringLiteral("deepseek-official");
    }
    if (baseUrl.contains(QStringLiteral("siliconflow.cn")) &&
        model == QStringLiteral("deepseek-ai/DeepSeek-OCR")) {
        return QStringLiteral("siliconflow-ocr");
    }
    return QStringLiteral("custom");
}

bool isKnownCloudPreset(const QString& id) {
    for (const auto& preset : cloudPresets()) {
        if (preset.id == id) return true;
    }
    return false;
}

QString keySlotValue(const AppSettings& settings, const QString& preset) {
    if (preset == QStringLiteral("siliconflow-ocr")) return settings.siliconflowApiKey;
    if (preset == QStringLiteral("custom")) return settings.customApiKey;
    return settings.deepseekApiKey;
}

}  // namespace

AppSettings loadSettings() {
    QSettings s(settingsPath(), QSettings::IniFormat);
    AppSettings out;
    out.autoCopy = s.value(QStringLiteral("autoCopy"), true).toBool();
    out.minScore = std::clamp(s.value(QStringLiteral("minScore"), 0.35).toDouble(), 0.0, 1.0);
    out.historyLimit = std::clamp(s.value(QStringLiteral("historyLimit"), 500).toInt(), 50, 5000);
    const QString modelDir = s.contains(QStringLiteral("ppocrModelDir"))
                                 ? s.value(QStringLiteral("ppocrModelDir")).toString()
                                 : s.value(QStringLiteral("modelDir")).toString();
    out.modelDir = normalizedModelDir(modelDir);
    out.ocrBackend = s.value(QStringLiteral("ocrBackend"), QStringLiteral("ppocr")).toString();
    out.tesseractLang = s.value(QStringLiteral("tesseractLang"), QStringLiteral("chi_sim+eng"))
                            .toString();
    out.cloudBaseUrl = s.value(QStringLiteral("cloudBaseUrl"),
                               QStringLiteral("https://api.deepseek.com"))
                           .toString();
    out.cloudModel = s.value(QStringLiteral("cloudModel"),
                             QStringLiteral("deepseek-flash"))
                         .toString();
    out.cloudPreset = s.value(QStringLiteral("cloudPreset")).toString();
    out.deepseekApiKey = s.value(QStringLiteral("deepseekApiKey")).toString();
    out.siliconflowApiKey = s.value(QStringLiteral("siliconflowApiKey")).toString();
    out.customApiKey = s.value(QStringLiteral("customApiKey")).toString();
    out.cloudPrompt = s.value(QStringLiteral("cloudPrompt"),
                              QStringLiteral("请识别图片中的所有文字，按原有行顺序输出，只输出文字，不要解释。"))
                          .toString();
    out.cloudTimeoutSec =
        std::clamp(s.value(QStringLiteral("cloudTimeoutSec"), 60).toInt(), 10, 300);

    if (!isKnownCloudPreset(out.cloudPreset)) {
        out.cloudPreset = inferCloudPreset(out.cloudBaseUrl, out.cloudModel);
    }

    // Migrate the pre-preset single key into the matching preset slot.
    const QString legacyKey = s.value(QStringLiteral("cloudApiKey")).toString();
    if (!legacyKey.isEmpty()) {
        if (out.cloudPreset == QStringLiteral("siliconflow-ocr") &&
            out.siliconflowApiKey.isEmpty()) {
            out.siliconflowApiKey = legacyKey;
        } else if (out.cloudPreset == QStringLiteral("custom") &&
                   out.customApiKey.isEmpty()) {
            out.customApiKey = legacyKey;
        } else if (out.deepseekApiKey.isEmpty()) {
            out.deepseekApiKey = legacyKey;
        }
        s.setValue(QStringLiteral("cloudPreset"), out.cloudPreset);
        s.setValue(QStringLiteral("deepseekApiKey"), out.deepseekApiKey);
        s.setValue(QStringLiteral("siliconflowApiKey"), out.siliconflowApiKey);
        s.setValue(QStringLiteral("customApiKey"), out.customApiKey);
        s.remove(QStringLiteral("cloudApiKey"));
        s.sync();
    }

    out.autostart = autostartEnabled();
    return out;
}

QString cloudApiKeyFor(const AppSettings& settings) {
    return keySlotValue(settings, settings.cloudPreset);
}

QString configuredModelDir() {
    QSettings s(settingsPath(), QSettings::IniFormat);
    const QString dir = s.contains(QStringLiteral("ppocrModelDir"))
                            ? s.value(QStringLiteral("ppocrModelDir")).toString()
                            : s.value(QStringLiteral("modelDir")).toString();
    return normalizedModelDir(dir);
}

void saveSettings(const AppSettings& settings) {
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("autoCopy"), settings.autoCopy);
    s.setValue(QStringLiteral("minScore"), settings.minScore);
    s.setValue(QStringLiteral("historyLimit"), settings.historyLimit);
    s.setValue(QStringLiteral("ppocrModelDir"), settings.modelDir);
    s.remove(QStringLiteral("modelDir"));
    s.setValue(QStringLiteral("ocrBackend"), settings.ocrBackend);
    s.setValue(QStringLiteral("tesseractLang"), settings.tesseractLang);
    s.setValue(QStringLiteral("cloudPreset"), settings.cloudPreset);
    s.setValue(QStringLiteral("cloudBaseUrl"), settings.cloudBaseUrl);
    s.setValue(QStringLiteral("cloudModel"), settings.cloudModel);
    s.setValue(QStringLiteral("deepseekApiKey"), settings.deepseekApiKey);
    s.setValue(QStringLiteral("siliconflowApiKey"), settings.siliconflowApiKey);
    s.setValue(QStringLiteral("customApiKey"), settings.customApiKey);
    s.setValue(QStringLiteral("cloudPrompt"), settings.cloudPrompt);
    s.setValue(QStringLiteral("cloudTimeoutSec"), settings.cloudTimeoutSec);
    s.remove(QStringLiteral("cloudApiKey"));
    s.sync();
}

SettingsDialog::SettingsDialog(const AppSettings& current, const QString& effectiveModelDir,
                               QWidget* parent)
    : QDialog(parent), effectiveModelDir_(effectiveModelDir) {
    setWindowTitle(QStringLiteral("Mini OCR 设置"));
    setMinimumWidth(580);

    auto* behavior = new QGroupBox(QStringLiteral("行为"), this);
    autoCopy_ = new QCheckBox(QStringLiteral("识别完成后自动复制到剪贴板"), behavior);
    autoCopy_->setChecked(current.autoCopy);
    auto* behaviorLayout = new QVBoxLayout(behavior);
    behaviorLayout->addWidget(autoCopy_);

    auto* ocr = new QGroupBox(QStringLiteral("识别"), this);
    backend_ = new QComboBox(ocr);
    for (const auto& backend : availableOcrBackends()) {
        backend_->addItem(backend.second, backend.first);
    }
    for (int i = 0; i < backend_->count(); ++i) {
        const QString id = backend_->itemData(i).toString();
        QString tip;
        if (id == QStringLiteral("tesseract")) {
            tip = QStringLiteral("适合英文/代码截图；中文精度低于 PP-OCR，"
                                 "需要安装 tesseract-data 语言包。");
        } else if (id == QStringLiteral("cloud")) {
            tip = QStringLiteral("默认 DeepSeek 官方 deepseek-flash，也可切到 "
                                 "SiliconFlow 的 DeepSeek-OCR；图片会上传，需要 API Key。");
        } else {
            tip = QStringLiteral("中文和中英混排效果最好，使用本地 ONNX 模型。");
        }
        backend_->setItemData(i, tip, Qt::ToolTipRole);
    }
    int backendIndex = backend_->findData(current.ocrBackend);
    if (backendIndex < 0) backendIndex = 0;
    backend_->setCurrentIndex(backendIndex);
    backend_->setToolTip(QStringLiteral("切换引擎后会立即重新加载。"));

    minScore_ = new QDoubleSpinBox(ocr);
    minScore_->setRange(0.0, 1.0);
    minScore_->setSingleStep(0.05);
    minScore_->setDecimals(2);
    minScore_->setValue(current.minScore);
    minScore_->setToolTip(
        QStringLiteral("低于该置信度的文本行会被丢弃。截图文字通常 0.8 以上，噪声较多时可调高。"));

    modelDir_ = new QLineEdit(current.modelDir, ocr);
    modelDir_->setPlaceholderText(QStringLiteral("自动查找"));
    auto* browse = new QPushButton(QStringLiteral("浏览…"), ocr);

    modelRow_ = new QWidget(ocr);
    auto* modelRowLayout = new QHBoxLayout(modelRow_);
    modelRowLayout->setContentsMargins(0, 0, 0, 0);
    modelRowLayout->addWidget(modelDir_, 1);
    modelRowLayout->addWidget(browse);

    tesseractLang_ = new QComboBox(ocr);
    tesseractLang_->setEditable(true);
    QStringList languages = availableTesseractLanguages();
    if (languages.isEmpty()) {
        languages = {QStringLiteral("chi_sim+eng"), QStringLiteral("chi_sim"),
                     QStringLiteral("eng"), QStringLiteral("jpn"),
                     QStringLiteral("chi_tra+eng"), QStringLiteral("osd")};
    }
    languages.removeDuplicates();
    tesseractLang_->addItems(languages);
    tesseractLang_->setCurrentText(current.tesseractLang);

    deepseekKey_ = current.deepseekApiKey;
    siliconflowKey_ = current.siliconflowApiKey;
    customKey_ = current.customApiKey;
    lastCloudPreset_ = current.cloudPreset;
    if (!isKnownCloudPreset(lastCloudPreset_)) {
        lastCloudPreset_ = inferCloudPreset(current.cloudBaseUrl, current.cloudModel);
    }

    cloudPanel_ = new QWidget(ocr);
    cloudPreset_ = new QComboBox(cloudPanel_);
    for (const auto& preset : cloudPresets()) {
        cloudPreset_->addItem(preset.label, preset.id);
    }
    cloudPreset_->setCurrentIndex(
        std::max(0, cloudPreset_->findData(lastCloudPreset_)));
    cloudPreset_->setToolTip(
        QStringLiteral("官方 deepseek-flash 是通用视觉模型；SiliconFlow 托管专门的 "
                       "DeepSeek-OCR 权重。"));

    cloudBaseUrl_ = new QLineEdit(current.cloudBaseUrl, cloudPanel_);
    cloudModel_ = new QLineEdit(current.cloudModel, cloudPanel_);
    cloudApiKey_ = new QLineEdit(keySlotValue(current, lastCloudPreset_), cloudPanel_);
    cloudApiKey_->setEchoMode(QLineEdit::Password);
    auto* showKey = new QCheckBox(QStringLiteral("显示"), cloudPanel_);
    connect(showKey, &QCheckBox::toggled, this, [this](bool on) {
        cloudApiKey_->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    });
    auto* clearKey = new QPushButton(QStringLiteral("清除"), cloudPanel_);
    connect(clearKey, &QPushButton::clicked, this, [this] { cloudApiKey_->clear(); });

    auto* keyRow = new QWidget(cloudPanel_);
    auto* keyRowLayout = new QHBoxLayout(keyRow);
    keyRowLayout->setContentsMargins(0, 0, 0, 0);
    keyRowLayout->addWidget(cloudApiKey_, 1);
    keyRowLayout->addWidget(showKey);
    keyRowLayout->addWidget(clearKey);

    cloudPrompt_ = new QLineEdit(current.cloudPrompt, cloudPanel_);
    cloudPrompt_->setToolTip(
        QStringLiteral("DeepSeek-OCR 纯文本提示词默认是 Free OCR.；"
                       "如果服务端不自动插入图片占位符，可改成 <image>\nFree OCR.；"
                       "改成 <image>\n<|grounding|>Convert the document to markdown. "
                       "可输出 Markdown。"));
    cloudTimeout_ = new QSpinBox(cloudPanel_);
    cloudTimeout_->setRange(10, 300);
    cloudTimeout_->setSuffix(QStringLiteral(" 秒"));
    cloudTimeout_->setValue(current.cloudTimeoutSec);

    auto* cloudHint = new QLabel(
        QStringLiteral("DeepSeek 官方用 deepseek-flash（通用视觉）；"
                       "SiliconFlow 托管专门的 DeepSeek-OCR。图片会上传，"
                       "Key 按预设分别保存，不会混用；API Key 以明文保存在本地配置。"
                       "<a href=\"https://platform.deepseek.com/api_keys\">DeepSeek Key</a> · "
                       "<a href=\"https://cloud.siliconflow.cn/account/ak\">SiliconFlow Key</a>"),
        cloudPanel_);
    cloudHint->setWordWrap(true);
    cloudHint->setOpenExternalLinks(true);
    cloudHint->setStyleSheet(QStringLiteral("color: #777;"));
    cloudHint->setMinimumHeight(cloudHint->fontMetrics().lineSpacing() * 3);

    auto* cloudForm = new QFormLayout(cloudPanel_);
    cloudForm->setContentsMargins(0, 0, 0, 0);
    cloudForm->addRow(QStringLiteral("预设"), cloudPreset_);
    cloudForm->addRow(QStringLiteral("服务地址"), cloudBaseUrl_);
    cloudForm->addRow(QStringLiteral("模型"), cloudModel_);
    cloudForm->addRow(QStringLiteral("API Key"), keyRow);
    cloudKeyLabel_ = qobject_cast<QLabel*>(cloudForm->labelForField(keyRow));
    updateCloudKeyLabel();
    cloudForm->addRow(QStringLiteral("提示词"), cloudPrompt_);
    cloudForm->addRow(QStringLiteral("超时"), cloudTimeout_);
    cloudForm->addRow(cloudHint);

    currentInfo_ = new QLabel(ocr);
    currentInfo_->setWordWrap(true);
    currentInfo_->setStyleSheet(QStringLiteral("color: #777;"));

    auto* engineHint = new QLabel(
        QStringLiteral("切换引擎会立即重新加载；各引擎的配置分别保留，切换回来不会丢失。"), ocr);
    engineHint->setWordWrap(true);
    engineHint->setStyleSheet(QStringLiteral("color: #777;"));

    ocrForm_ = new QFormLayout(ocr);
    ocrForm_->addRow(QStringLiteral("识别引擎"), backend_);
    ocrForm_->addRow(engineHint);
    ocrForm_->addRow(QStringLiteral("最低置信度"), minScore_);
    ocrForm_->addRow(QStringLiteral("模型目录"), modelRow_);
    ocrForm_->addRow(QStringLiteral("Tesseract 语言"), tesseractLang_);
    ocrForm_->addRow(cloudPanel_);
    ocrForm_->addRow(currentInfo_);

    auto updateBackendRows = [this] {
        const QString backendId = backend_->currentData().toString();
        const bool isPpocr = backendId == QStringLiteral("ppocr");
        const bool isTesseract = backendId == QStringLiteral("tesseract");
        const bool isCloud = backendId == QStringLiteral("cloud");
        ocrForm_->setRowVisible(modelRow_, isPpocr);
        ocrForm_->setRowVisible(tesseractLang_, isTesseract);
        ocrForm_->setRowVisible(cloudPanel_, isCloud);
        if (isPpocr) {
            const QString dir = normalizedModelDir(modelDir_->text());
            currentInfo_->setText(
                QStringLiteral("当前使用：%1")
                    .arg(!dir.isEmpty() ? dir
                                        : (effectiveModelDir_.isEmpty()
                                               ? QStringLiteral("（未找到模型）")
                                               : effectiveModelDir_)));
        } else if (isTesseract) {
            currentInfo_->setText(
                QStringLiteral("Tesseract 语言：%1 · 使用系统 tessdata 目录")
                    .arg(tesseractLang_->currentText()));
        } else if (isCloud) {
            const QUrl url(cloudBaseUrl_->text().trimmed());
            currentInfo_->setText(
                QStringLiteral("云端：%1 @ %2 · 图片会上传")
                    .arg(cloudModel_->text().trimmed().isEmpty()
                             ? QStringLiteral("（未设置模型）")
                             : cloudModel_->text().trimmed(),
                         url.host().isEmpty() ? cloudBaseUrl_->text().trimmed()
                                              : url.host()));
        } else {
            currentInfo_->clear();
        }
    };
    connect(backend_, &QComboBox::currentIndexChanged, this,
            [this, updateBackendRows](int) {
                updateBackendRows();
                QTimer::singleShot(0, this, [this] {
                    if (!isVisible()) return;
                    layout()->activate();
                    resize(width(), sizeHint().height());
                });
            });
    connect(tesseractLang_, &QComboBox::currentTextChanged, this,
            [updateBackendRows](const QString&) { updateBackendRows(); });
    connect(modelDir_, &QLineEdit::textChanged, this,
            [updateBackendRows](const QString&) { updateBackendRows(); });
    connect(cloudPreset_, &QComboBox::activated, this, [this](int index) {
        const QString presetId = cloudPreset_->itemData(index).toString();
        if (presetId == QStringLiteral("custom")) {
            storeCurrentCloudKey();
            lastCloudPreset_ = presetId;
            loadCloudKey(presetId);
            updateCloudKeyLabel();
            return;
        }
        for (const auto& preset : cloudPresets()) {
            if (preset.id != presetId || preset.baseUrl.isEmpty()) continue;
            storeCurrentCloudKey();
            presetUpdating_ = true;
            cloudBaseUrl_->setText(preset.baseUrl);
            cloudModel_->setText(preset.model);
            cloudPrompt_->setText(preset.prompt);
            presetUpdating_ = false;
            lastCloudPreset_ = presetId;
            loadCloudKey(presetId);
            updateCloudKeyLabel();
            break;
        }
    });

    auto syncPresetWithFields = [this] {
        if (presetUpdating_) return;
        const QString presetId = cloudPreset_->currentData().toString();
        for (const auto& preset : cloudPresets()) {
            if (preset.id != presetId || preset.baseUrl.isEmpty()) continue;
            if (cloudBaseUrl_->text().trimmed() != preset.baseUrl ||
                cloudModel_->text().trimmed() != preset.model) {
                storeCurrentCloudKey();
                lastCloudPreset_ = QStringLiteral("custom");
                const QSignalBlocker blocker(cloudPreset_);
                cloudPreset_->setCurrentIndex(cloudPreset_->findData(lastCloudPreset_));
                loadCloudKey(lastCloudPreset_);
                updateCloudKeyLabel();
            }
            return;
        }
    };
    connect(cloudBaseUrl_, &QLineEdit::textChanged, this,
            [updateBackendRows, syncPresetWithFields](const QString&) {
                syncPresetWithFields();
                updateBackendRows();
            });
    connect(cloudModel_, &QLineEdit::textChanged, this,
            [updateBackendRows, syncPresetWithFields](const QString&) {
                syncPresetWithFields();
                updateBackendRows();
            });
    updateBackendRows();

    auto* history = new QGroupBox(QStringLiteral("历史记录"), this);
    historyLimit_ = new QSpinBox(history);
    historyLimit_->setRange(50, 5000);
    historyLimit_->setSingleStep(50);
    historyLimit_->setValue(current.historyLimit);
    historyLimit_->setSuffix(QStringLiteral(" 条"));
    auto* historyForm = new QFormLayout(history);
    historyForm->addRow(QStringLiteral("最多保留"), historyLimit_);

    auto* system = new QGroupBox(QStringLiteral("系统"), this);
    autostart_ = new QCheckBox(QStringLiteral("登录时自动启动 Mini OCR"), system);
    autostart_->setChecked(current.autostart);

#ifdef Q_OS_WIN
    auto* shortcutHint = new QLabel(
        QStringLiteral("Windows 全局快捷键：Ctrl+Alt+O（由应用直接注册）"), system);
    shortcutHint->setWordWrap(true);
    auto* shortcutEdit = new QLineEdit(QStringLiteral("Ctrl+Alt+O"), system);
#else
    QString shortcutCommand = QCoreApplication::applicationFilePath();
    if (shortcutCommand.contains(QLatin1Char(' '))) {
        shortcutCommand = QLatin1Char('"') + shortcutCommand + QLatin1Char('"');
    }
    shortcutCommand += QStringLiteral(" capture");

    auto* shortcutHint = new QLabel(
        QStringLiteral("全局快捷键在 KDE 系统设置 → 快捷键 → 自定义快捷键中绑定："), system);
    shortcutHint->setWordWrap(true);
    auto* shortcutEdit = new QLineEdit(shortcutCommand, system);
#endif
    shortcutEdit->setReadOnly(true);
    shortcutEdit->setCursorPosition(0);

    auto* systemLayout = new QVBoxLayout(system);
    systemLayout->addWidget(autostart_);
    systemLayout->addWidget(shortcutHint);
    systemLayout->addWidget(shortcutEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(behavior);
    layout->addWidget(ocr);
    layout->addWidget(history);
    layout->addWidget(system);
    layout->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, [this] { browseModelDir(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        const QString backendId = backend_->currentData().toString();
        if (backendId == QStringLiteral("ppocr")) {
            const QString dir = normalizedModelDir(modelDir_->text());
            if (!dir.isEmpty() && !looksLikeModelDir(dir)) {
                QMessageBox::warning(this, QStringLiteral("模型目录无效"),
                                     QStringLiteral("该目录下缺少 PP-OCRv5 模型文件：\n"
                                                    "ch_PP-OCRv5_det_mobile.onnx\n"
                                                    "ch_PP-OCRv5_rec_mobile.onnx\n"
                                                    "ppocrv5_dict.txt"));
                return;
            }
        } else if (backendId == QStringLiteral("tesseract")) {
            if (tesseractLang_->currentText().trimmed().isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("语言无效"),
                                     QStringLiteral("Tesseract 语言不能为空，例如 chi_sim+eng。"));
                return;
            }
        } else if (backendId == QStringLiteral("cloud")) {
            if (cloudApiKey_->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("缺少 API Key"),
                                     QStringLiteral("云端 OCR 需要 API Key。"));
                return;
            }
            const QUrl url(cloudBaseUrl_->text().trimmed());
            if (!url.isValid() || (url.scheme() != QStringLiteral("http") &&
                                   url.scheme() != QStringLiteral("https"))) {
                QMessageBox::warning(this, QStringLiteral("服务地址无效"),
                                     QStringLiteral("请填写 http:// 或 https:// 开头的地址。"));
                return;
            }
            if (cloudModel_->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("模型为空"),
                                     QStringLiteral("请填写模型名，例如 deepseek-ai/DeepSeek-OCR。"));
                return;
            }
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    QTimer::singleShot(0, this, [this] {
        layout()->activate();
        resize(width(), sizeHint().height());
    });
}

void SettingsDialog::storeCurrentCloudKey() {
    if (!cloudApiKey_) return;
    const QString key = cloudApiKey_->text().trimmed();
    if (lastCloudPreset_ == QStringLiteral("siliconflow-ocr")) {
        siliconflowKey_ = key;
    } else if (lastCloudPreset_ == QStringLiteral("custom")) {
        customKey_ = key;
    } else {
        deepseekKey_ = key;
    }
}

void SettingsDialog::loadCloudKey(const QString& preset) {
    if (!cloudApiKey_) return;
    if (preset == QStringLiteral("siliconflow-ocr")) {
        cloudApiKey_->setText(siliconflowKey_);
    } else if (preset == QStringLiteral("custom")) {
        cloudApiKey_->setText(customKey_);
    } else {
        cloudApiKey_->setText(deepseekKey_);
    }
}

void SettingsDialog::updateCloudKeyLabel() {
    if (!cloudKeyLabel_) return;
    if (lastCloudPreset_ == QStringLiteral("siliconflow-ocr")) {
        cloudKeyLabel_->setText(QStringLiteral("SiliconFlow API Key"));
    } else if (lastCloudPreset_ == QStringLiteral("custom")) {
        cloudKeyLabel_->setText(QStringLiteral("自定义 API Key"));
    } else {
        cloudKeyLabel_->setText(QStringLiteral("DeepSeek API Key"));
    }
}

void SettingsDialog::browseModelDir() {
    const QString start = modelDir_->text().isEmpty()
                              ? QDir::homePath()
                              : normalizedModelDir(modelDir_->text());
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择模型目录"), start);
    if (!dir.isEmpty()) modelDir_->setText(dir);
}

AppSettings SettingsDialog::settings() {
    AppSettings out;
    out.autoCopy = autoCopy_->isChecked();
    out.minScore = minScore_->value();
    out.historyLimit = historyLimit_->value();
    out.autostart = autostart_->isChecked();
    out.modelDir = normalizedModelDir(modelDir_->text());
    out.ocrBackend = backend_->currentData().toString();
    if (out.ocrBackend.isEmpty()) out.ocrBackend = QStringLiteral("ppocr");
    out.tesseractLang = tesseractLang_->currentText().trimmed();
    if (out.tesseractLang.isEmpty()) out.tesseractLang = QStringLiteral("chi_sim+eng");
    storeCurrentCloudKey();
    out.cloudPreset = lastCloudPreset_;
    out.deepseekApiKey = deepseekKey_;
    out.siliconflowApiKey = siliconflowKey_;
    out.customApiKey = customKey_;
    out.cloudBaseUrl = cloudBaseUrl_->text().trimmed();
    if (out.cloudBaseUrl.isEmpty()) {
        out.cloudBaseUrl = QStringLiteral("https://api.deepseek.com");
    }
    out.cloudModel = cloudModel_->text().trimmed();
    if (out.cloudModel.isEmpty()) out.cloudModel = QStringLiteral("deepseek-flash");
    out.cloudPrompt = cloudPrompt_->text().trimmed();
    if (out.cloudPrompt.isEmpty()) {
        out.cloudPrompt = QStringLiteral("请识别图片中的所有文字，按原有行顺序输出，只输出文字，不要解释。");
    }
    out.cloudTimeoutSec = cloudTimeout_->value();
    return out;
}
