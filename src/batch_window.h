#pragma once

#include <QImage>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QLabel;
class ImageView;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QDragEnterEvent;
class QDropEvent;

class BatchWindow : public QWidget {
public:
    explicit BatchWindow(const QStringList& paths = {}, QWidget* parent = nullptr);

    void setStartCallback(std::function<void()> callback) {
        startCallback_ = std::move(callback);
    }
    void setCancelCallback(std::function<void()> callback) {
        cancelCallback_ = std::move(callback);
    }

    void appendFiles(const QStringList& paths);
    void resetForRun();
    void setStatus(int index, const QString& status);
    void setResult(int index, const QString& text);
    void setError(int index, const QString& error);
    void setProgress(int done);
    void markCancelling();
    void finishRun(bool cancelled);

    QStringList paths() const { return paths_; }

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void addFiles();
    void removeSelected();
    void clearFiles();
    void refreshItem(int index);
    void updatePreview();
    void updateActionStates();
    QString allText() const;
    void copyCurrent();
    void copyAll();
    void exportAll();

    QStringList paths_;
    QImage currentImage_;
    int currentImageIndex_ = -1;
    QVector<QString> texts_;
    QVector<QString> errors_;
    QVector<QString> statuses_;

    QListWidget* list_ = nullptr;
    ImageView* imagePreview_ = nullptr;
    QPlainTextEdit* preview_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QPushButton* copyCurrentButton_ = nullptr;
    QPushButton* copyAllButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;

    std::function<void()> startCallback_;
    std::function<void()> cancelCallback_;
    bool running_ = false;
    bool cancelled_ = false;
    bool cancelling_ = false;
};
