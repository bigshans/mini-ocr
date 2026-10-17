#include "batch_window.h"

#include "image_view.h"

#include <QAbstractItemView>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QSplitter>
#include <QStandardPaths>
#include <QSizePolicy>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <QSet>

BatchWindow::BatchWindow(const QStringList& paths, QWidget* parent)
    : QWidget(parent, Qt::Window) {
    setAttribute(Qt::WA_DeleteOnClose);
    setAcceptDrops(true);
    setWindowTitle(QStringLiteral("图片 OCR — Mini OCR"));
    resize(1000, 640);

    auto* hint = new QLabel(
        QStringLiteral("拖入图片，或点击“添加图片”。选好图片后点击“开始识别”；"
                       "可以随时取消当前批次。"),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #777;"));

    list_ = new QListWidget(this);
    list_->setMinimumWidth(300);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);

    imagePreview_ = new ImageView(this);
    imagePreview_->setMinimumHeight(220);
    imagePreview_->setToolTip(QStringLiteral("滚轮缩放，拖拽平移"));

    auto* zoomOutButton = new QPushButton(QStringLiteral("-"), this);
    auto* fitButton = new QPushButton(QStringLiteral("适应"), this);
    auto* zoomInButton = new QPushButton(QStringLiteral("+"), this);
    zoomOutButton->setFixedWidth(34);
    zoomInButton->setFixedWidth(34);
    zoomOutButton->setToolTip(QStringLiteral("缩小"));
    fitButton->setToolTip(QStringLiteral("适应窗口"));
    zoomInButton->setToolTip(QStringLiteral("放大"));

    auto* zoomLabel = new QLabel(QStringLiteral("适应"), this);
    zoomLabel->setAlignment(Qt::AlignCenter);
    zoomLabel->setMinimumWidth(52);
    zoomLabel->setStyleSheet(QStringLiteral("color: #666;"));

    auto* imageControls = new QHBoxLayout;
    imageControls->setContentsMargins(0, 0, 0, 0);
    imageControls->addWidget(new QLabel(QStringLiteral("图片预览"), this));
    imageControls->addStretch();
    imageControls->addWidget(zoomOutButton);
    imageControls->addWidget(fitButton);
    imageControls->addWidget(zoomInButton);
    imageControls->addWidget(zoomLabel);

    auto* imagePane = new QWidget(this);
    auto* imageLayout = new QVBoxLayout(imagePane);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    imageLayout->setSpacing(4);
    imageLayout->addLayout(imageControls);
    imageLayout->addWidget(imagePreview_, 1);

    connect(zoomOutButton, &QPushButton::clicked, imagePreview_, &ImageView::zoomOut);
    connect(fitButton, &QPushButton::clicked, imagePreview_, &ImageView::fitToWindow);
    connect(zoomInButton, &QPushButton::clicked, imagePreview_, &ImageView::zoomIn);
    imagePreview_->setZoomChangedCallback([zoomLabel](int percent, bool fit) {
        zoomLabel->setText(fit ? QStringLiteral("适应")
                               : QStringLiteral("%1%").arg(percent));
    });

    preview_ = new QPlainTextEdit(this);
    preview_->setReadOnly(true);
    preview_->setLineWrapMode(QPlainTextEdit::NoWrap);
    preview_->setPlaceholderText(QStringLiteral("识别结果会显示在这里…"));

    auto* rightSide = new QSplitter(Qt::Vertical, this);
    rightSide->addWidget(imagePane);
    rightSide->addWidget(preview_);
    rightSide->setStretchFactor(0, 3);
    rightSide->setStretchFactor(1, 2);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(list_);
    splitter->addWidget(rightSide);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 680});

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 1);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    statusLabel_ = new QLabel(QStringLiteral("0 / 0"), this);
    statusLabel_->setStyleSheet(QStringLiteral("color: #666;"));

    auto* progressRow = new QHBoxLayout;
    progressRow->addWidget(progress_, 1);
    progressRow->addWidget(statusLabel_);

    addButton_ = new QPushButton(QStringLiteral("添加图片…"), this);
    removeButton_ = new QPushButton(QStringLiteral("移除选中"), this);
    clearButton_ = new QPushButton(QStringLiteral("清空"), this);
    startButton_ = new QPushButton(QStringLiteral("开始识别"), this);
    cancelButton_ = new QPushButton(QStringLiteral("取消"), this);
    copyCurrentButton_ = new QPushButton(QStringLiteral("复制当前"), this);
    copyAllButton_ = new QPushButton(QStringLiteral("复制全部"), this);
    exportButton_ = new QPushButton(QStringLiteral("导出 TXT"), this);
    auto* closeButton = new QPushButton(QStringLiteral("关闭"), this);

    auto* fileActions = new QHBoxLayout;
    fileActions->addWidget(addButton_);
    fileActions->addWidget(removeButton_);
    fileActions->addWidget(clearButton_);
    fileActions->addStretch();

    auto* runActions = new QHBoxLayout;
    runActions->addWidget(startButton_);
    runActions->addWidget(cancelButton_);
    runActions->addStretch();
    runActions->addWidget(copyCurrentButton_);
    runActions->addWidget(copyAllButton_);
    runActions->addWidget(exportButton_);
    runActions->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    layout->addWidget(hint);
    layout->addLayout(fileActions);
    layout->addWidget(splitter, 1);
    layout->addLayout(progressRow);
    layout->addLayout(runActions);

    connect(list_, &QListWidget::currentRowChanged, this, [this](int) {
        updatePreview();
        updateActionStates();
    });
    connect(addButton_, &QPushButton::clicked, this, [this] { addFiles(); });
    connect(removeButton_, &QPushButton::clicked, this, [this] { removeSelected(); });
    connect(clearButton_, &QPushButton::clicked, this, [this] { clearFiles(); });
    connect(startButton_, &QPushButton::clicked, this, [this] {
        if (running_ || !startCallback_) return;
        cancelled_ = false;
        startCallback_();
    });
    connect(cancelButton_, &QPushButton::clicked, this, [this] {
        if (!running_) return;
        cancelled_ = true;
        markCancelling();
        if (cancelCallback_) cancelCallback_();
    });
    connect(copyCurrentButton_, &QPushButton::clicked, this, [this] { copyCurrent(); });
    connect(copyAllButton_, &QPushButton::clicked, this, [this] { copyAll(); });
    connect(exportButton_, &QPushButton::clicked, this, [this] { exportAll(); });
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

    appendFiles(paths);
    updateActionStates();
}

void BatchWindow::appendFiles(const QStringList& paths) {
    if (running_) return;

    QSet<QString> existing;
    for (const QString& path : paths_) existing.insert(path);

    bool added = false;
    for (const QString& raw : paths) {
        const QFileInfo info(raw);
        if (!info.isFile()) continue;
        const QString path = info.absoluteFilePath();
        if (existing.contains(path)) continue;

        QImageReader reader(path);
        reader.setAutoTransform(true);
        if (!reader.canRead()) continue;

        existing.insert(path);
        paths_ << path;
        texts_ << QString();
        errors_ << QString();
        statuses_ << QStringLiteral("等待");

        auto* item = new QListWidgetItem(list_);
        item->setData(Qt::UserRole, paths_.size() - 1);
        refreshItem(paths_.size() - 1);
        added = true;
    }

    if (added) {
        progress_->setRange(0, std::max(1, static_cast<int>(paths_.size())));
        progress_->setValue(0);
        statusLabel_->setText(QStringLiteral("0 / %1").arg(paths_.size()));
        if (list_->currentRow() < 0 && list_->count() > 0) list_->setCurrentRow(0);
    }
    updateActionStates();
}

void BatchWindow::addFiles() {
    if (running_) return;
    QString startDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (!paths_.isEmpty()) startDir = QFileInfo(paths_.first()).absolutePath();
    if (startDir.isEmpty()) startDir = QDir::homePath();

    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("添加图片"), startDir,
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff);;所有文件 (*)"));
    if (!files.isEmpty()) appendFiles(files);
}

void BatchWindow::removeSelected() {
    if (running_) return;
    const QList<QListWidgetItem*> selected = list_->selectedItems();
    if (selected.isEmpty()) return;

    QVector<int> rows;
    for (QListWidgetItem* item : selected) rows << list_->row(item);
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    for (int row : rows) {
        delete list_->takeItem(row);
        paths_.removeAt(row);
        texts_.removeAt(row);
        errors_.removeAt(row);
        statuses_.removeAt(row);
    }

    progress_->setRange(0, std::max(1, static_cast<int>(paths_.size())));
    progress_->setValue(0);
    statusLabel_->setText(QStringLiteral("0 / %1").arg(paths_.size()));
    if (list_->count() > 0) {
        list_->setCurrentRow(std::min(rows.isEmpty() ? 0 : rows.last(), list_->count() - 1));
    } else {
        updatePreview();
    }
    updateActionStates();
}

void BatchWindow::clearFiles() {
    if (running_ || paths_.isEmpty()) return;
    const auto answer = QMessageBox::question(
        this, QStringLiteral("图片 OCR"),
        QStringLiteral("清空已添加的图片和识别结果？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    list_->clear();
    paths_.clear();
    texts_.clear();
    errors_.clear();
    statuses_.clear();
    preview_->clear();
    progress_->setRange(0, 1);
    progress_->setValue(0);
    statusLabel_->setText(QStringLiteral("0 / 0"));
    updatePreview();
    updateActionStates();
}

void BatchWindow::resetForRun() {
    running_ = true;
    cancelled_ = false;
    cancelling_ = false;
    for (int i = 0; i < statuses_.size(); ++i) {
        texts_[i].clear();
        errors_[i].clear();
        statuses_[i] = QStringLiteral("等待");
        refreshItem(i);
    }
    preview_->clear();
    progress_->setRange(0, std::max(1, static_cast<int>(paths_.size())));
    progress_->setValue(0);
    statusLabel_->setText(QStringLiteral("0 / %1").arg(paths_.size()));
    updateActionStates();
    if (!paths_.isEmpty()) list_->setCurrentRow(0);
    updatePreview();
}

void BatchWindow::setStatus(int index, const QString& status) {
    if (index < 0 || index >= statuses_.size()) return;
    statuses_[index] = status;
    refreshItem(index);
    updateActionStates();
}

void BatchWindow::setResult(int index, const QString& text) {
    if (index < 0 || index >= statuses_.size()) return;
    texts_[index] = text;
    errors_[index].clear();
    statuses_[index] = QStringLiteral("完成");
    refreshItem(index);
    updatePreview();
    updateActionStates();
}

void BatchWindow::setError(int index, const QString& error) {
    if (index < 0 || index >= statuses_.size()) return;
    errors_[index] = error;
    texts_[index].clear();
    statuses_[index] = QStringLiteral("失败");
    refreshItem(index);
    updatePreview();
    updateActionStates();
}

void BatchWindow::setProgress(int done) {
    progress_->setValue(std::min(done, static_cast<int>(paths_.size())));
    statusLabel_->setText(QStringLiteral("%1 / %2").arg(done).arg(paths_.size()));
}

void BatchWindow::markCancelling() {
    cancelling_ = true;
    statusLabel_->setText(QStringLiteral("正在取消…"));
    updateActionStates();
}

void BatchWindow::finishRun(bool cancelled) {
    running_ = false;
    cancelling_ = false;
    cancelled_ = cancelled;
    if (cancelled) {
        for (int i = 0; i < statuses_.size(); ++i) {
            if (statuses_[i] == QStringLiteral("等待") ||
                statuses_[i] == QStringLiteral("识别中…") ||
                statuses_[i] == QStringLiteral("取消中…")) {
                statuses_[i] = QStringLiteral("已跳过");
                refreshItem(i);
            }
        }
        statusLabel_->setText(QStringLiteral("已取消"));
    } else {
        statusLabel_->setText(QStringLiteral("完成 %1 / %2")
                                  .arg(progress_->value())
                                  .arg(paths_.size()));
    }
    updateActionStates();
}

void BatchWindow::refreshItem(int index) {
    if (index < 0 || index >= paths_.size()) return;
    QListWidgetItem* item = list_->item(index);
    if (!item) return;
    const QString name = QFileInfo(paths_[index]).fileName();
    item->setText(QStringLiteral("%1  [%2]").arg(name, statuses_[index]));
    item->setToolTip(paths_[index]);
    if (!errors_[index].isEmpty()) {
        item->setForeground(QBrush(QColor(QStringLiteral("#c0392b"))));
    } else if (statuses_[index] == QStringLiteral("完成")) {
        item->setForeground(QBrush(QColor(QStringLiteral("#1e8449"))));
    } else if (statuses_[index] == QStringLiteral("识别中…")) {
        item->setForeground(QBrush(QColor(QStringLiteral("#2d7ff9"))));
    } else {
        item->setForeground(QBrush());
    }
}

void BatchWindow::updatePreview() {
    const int row = list_->currentRow();
    if (row < 0 || row >= texts_.size()) {
        currentImage_ = QImage();
        currentImageIndex_ = -1;
        imagePreview_->clearImage();
        preview_->clear();
        return;
    }

    if (row != currentImageIndex_) {
        QImageReader reader(paths_[row]);
        reader.setAutoTransform(true);
        currentImage_ = reader.read();
        currentImageIndex_ = row;
        if (currentImage_.isNull()) {
            imagePreview_->clearImage();
        } else {
            imagePreview_->setImage(currentImage_);
        }
    }

    if (!errors_[row].isEmpty()) {
        preview_->setPlainText(QStringLiteral("[失败]\n%1").arg(errors_[row]));
    } else if (!texts_[row].isEmpty()) {
        preview_->setPlainText(texts_[row]);
    } else {
        preview_->setPlainText(QStringLiteral("（尚未识别）"));
    }
}

void BatchWindow::updateActionStates() {
    const bool hasPaths = !paths_.isEmpty();
    addButton_->setEnabled(!running_);
    removeButton_->setEnabled(!running_ && hasPaths);
    clearButton_->setEnabled(!running_ && hasPaths);
    startButton_->setEnabled(!running_ && hasPaths);
    cancelButton_->setEnabled(running_ && !cancelling_);

    bool hasResult = false;
    for (int i = 0; i < texts_.size(); ++i) {
        if (!texts_[i].isEmpty() || !errors_[i].isEmpty()) {
            hasResult = true;
            break;
        }
    }
    copyAllButton_->setEnabled(hasResult);
    exportButton_->setEnabled(hasResult);
    const int row = list_->currentRow();
    copyCurrentButton_->setEnabled(
        row >= 0 && row < texts_.size() && !texts_[row].isEmpty());
}

QString BatchWindow::allText() const {
    QString out;
    for (int i = 0; i < paths_.size(); ++i) {
        if (texts_[i].isEmpty() && errors_[i].isEmpty()) continue;
        out += QStringLiteral("===== %1 =====\n").arg(QFileInfo(paths_[i]).fileName());
        if (!errors_[i].isEmpty()) {
            out += QStringLiteral("[失败] %1\n\n").arg(errors_[i]);
        } else {
            out += texts_[i];
            out += QStringLiteral("\n\n");
        }
    }
    return out.trimmed();
}

void BatchWindow::copyCurrent() {
    const int row = list_->currentRow();
    if (row < 0 || row >= texts_.size() || texts_[row].isEmpty()) return;
    QGuiApplication::clipboard()->setText(texts_[row]);
    statusLabel_->setText(QStringLiteral("已复制当前结果"));
}

void BatchWindow::copyAll() {
    const QString text = allText();
    if (text.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("图片 OCR"),
                                 QStringLiteral("还没有可复制的结果。"));
        return;
    }
    QGuiApplication::clipboard()->setText(text);
    statusLabel_->setText(QStringLiteral("已复制全部结果"));
}

void BatchWindow::exportAll() {
    const QString text = allText();
    if (text.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("图片 OCR"),
                                 QStringLiteral("还没有可导出的结果。"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出 OCR 结果"),
        QDir::homePath() + QStringLiteral("/ocr-results.txt"),
        QStringLiteral("文本文件 (*.txt)"));
    if (path.isEmpty()) return;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                             QStringLiteral("无法写入：%1").arg(path));
        return;
    }
    file.write(text.toUtf8());
    file.write("\n");
    if (!file.commit()) {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                             QStringLiteral("保存失败：%1").arg(path));
        return;
    }
    statusLabel_->setText(QStringLiteral("已导出：%1").arg(QFileInfo(path).fileName()));
}

void BatchWindow::closeEvent(QCloseEvent* event) {
    if (running_) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("图片 OCR"),
            QStringLiteral("正在识别，停止并关闭窗口？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        cancelled_ = true;
        if (cancelCallback_) cancelCallback_();
    }
    event->accept();
}

void BatchWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!running_ && event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void BatchWindow::dropEvent(QDropEvent* event) {
    if (running_ || !event->mimeData()->hasUrls()) return;
    QStringList files;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) files << url.toLocalFile();
    }
    appendFiles(files);
    event->acceptProposedAction();
}
