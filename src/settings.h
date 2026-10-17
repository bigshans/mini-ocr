#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QSpinBox;
class QWidget;

struct AppSettings {
    bool autoCopy = true;
    double minScore = 0.35;
    int historyLimit = 500;
    bool autostart = false;
    QString modelDir;  // empty = auto-detect
    QString ocrBackend = QStringLiteral("ppocr");
    QString tesseractLang = QStringLiteral("chi_sim+eng");
    QString cloudPreset = QStringLiteral("deepseek-official");
    QString cloudBaseUrl = QStringLiteral("https://api.deepseek.com");
    QString cloudModel = QStringLiteral("deepseek-flash");
    QString deepseekApiKey;
    QString siliconflowApiKey;
    QString customApiKey;
    QString cloudPrompt = QStringLiteral("请识别图片中的所有文字，按原有行顺序输出，只输出文字，不要解释。");
    int cloudTimeoutSec = 60;
};

AppSettings loadSettings();
QString configuredModelDir();
QString cloudApiKeyFor(const AppSettings& settings);
void saveSettings(const AppSettings& settings);

QString autostartFilePath();
bool autostartEnabled();
bool setAutostartEnabled(bool enabled, QString* error);

class SettingsDialog : public QDialog {
public:
    SettingsDialog(const AppSettings& current, const QString& effectiveModelDir,
                   QWidget* parent = nullptr);

    AppSettings settings();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void browseModelDir();

    QString effectiveModelDir_;
    QCheckBox* autoCopy_ = nullptr;
    QComboBox* backend_ = nullptr;
    QDoubleSpinBox* minScore_ = nullptr;
    QSpinBox* historyLimit_ = nullptr;
    QCheckBox* autostart_ = nullptr;
    QLineEdit* modelDir_ = nullptr;
    QWidget* modelRow_ = nullptr;
    QComboBox* tesseractLang_ = nullptr;
    QWidget* cloudPanel_ = nullptr;
    QComboBox* cloudPreset_ = nullptr;
    QLineEdit* cloudBaseUrl_ = nullptr;
    QLineEdit* cloudModel_ = nullptr;
    QLineEdit* cloudApiKey_ = nullptr;
    QLabel* cloudKeyLabel_ = nullptr;
    QLineEdit* cloudPrompt_ = nullptr;
    QSpinBox* cloudTimeout_ = nullptr;
    QLabel* currentInfo_ = nullptr;
    QFormLayout* ocrForm_ = nullptr;
    bool presetUpdating_ = false;

    void storeCurrentCloudKey();
    void loadCloudKey(const QString& preset);
    void updateCloudKeyLabel();

    QString lastCloudPreset_;
    QString deepseekKey_;
    QString siliconflowKey_;
    QString customKey_;
};
