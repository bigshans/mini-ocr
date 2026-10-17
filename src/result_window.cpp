#include "result_window.h"

#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QVBoxLayout>

ResultWindow::ResultWindow(const QString& text, const QVector<OcrLine>& lines,
                           bool alreadyCopied, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::WindowStaysOnTopHint) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("OCR 结果"));
    setMinimumSize(560, 300);
    resize(720, 440);

    double sum = 0.0;
    for (const auto& line : lines) sum += line.score;
    const double avg = lines.isEmpty() ? 0.0 : sum / lines.size();

    const bool cloudResult = !lines.isEmpty() && lines.first().box.isNull();
    auto* summary = new QLabel(
        cloudResult
            ? QStringLiteral("云端识别到 %1 行").arg(lines.size())
            : QStringLiteral("识别到 %1 行 · 平均置信度 %2%")
                  .arg(lines.size())
                  .arg(QString::number(avg * 100.0, 'f', 0)),
        this);
    summary->setStyleSheet(QStringLiteral("color: #666; padding: 2px;"));

    edit_ = new QPlainTextEdit(text, this);
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit_->setTabChangesFocus(false);
    QFont f = edit_->font();
    f.setPointSize(11);
    edit_->setFont(f);

    status_ = new QLabel(alreadyCopied ? QStringLiteral("已复制到剪贴板")
                                       : QStringLiteral("Esc 关闭 · Ctrl+Enter 复制"),
                         this);
    status_->setStyleSheet(QStringLiteral("color: #2d7ff9; padding: 2px;"));

    auto* copyButton = new QPushButton(QStringLiteral("复制"), this);
    copyButton->setDefault(true);
    auto* closeButton = new QPushButton(QStringLiteral("关闭"), this);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(status_, 1);
    buttons->addWidget(copyButton);
    buttons->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    layout->addWidget(summary);
    layout->addWidget(edit_, 1);
    layout->addLayout(buttons);

    connect(copyButton, &QPushButton::clicked, this, [this] { copyToClipboard(); });
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(esc, &QShortcut::activated, this, &QWidget::close);
    auto* copyShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Return")), this);
    connect(copyShortcut, &QShortcut::activated, this, [this] { copyToClipboard(); });

    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect avail = screen->availableGeometry();
        move(avail.center() - rect().center());
    }

    edit_->selectAll();
    edit_->setFocus();
}

void ResultWindow::copyToClipboard() {
    QGuiApplication::clipboard()->setText(edit_->toPlainText());
    status_->setText(QStringLiteral("已复制到剪贴板"));
}
