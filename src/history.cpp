#include "history.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>

bool HistoryStore::open(QString* error) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(dir)) {
        if (error) *error = QStringLiteral("无法创建数据目录: %1").arg(dir);
        return false;
    }

    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                    QStringLiteral("mini-ocr-history"));
    db_.setDatabaseName(dir + QStringLiteral("/history.db"));
    if (!db_.open()) {
        if (error) *error = db_.lastError().text();
        return false;
    }

    QSqlQuery q(db_);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS history ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "created_at TEXT NOT NULL,"
            "text TEXT NOT NULL)"))) {
        if (error) *error = q.lastError().text();
        return false;
    }
    return true;
}

bool HistoryStore::add(const QString& text) {
    if (!db_.isOpen() || text.isEmpty()) return false;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("INSERT INTO history(created_at, text) VALUES(?, ?)"));
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    q.addBindValue(text);
    if (!q.exec()) return false;

    prune();
    return true;
}

void HistoryStore::setLimit(int limit) {
    limit_ = std::max(50, limit);
    prune();
}

void HistoryStore::prune() {
    if (!db_.isOpen()) return;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "DELETE FROM history WHERE id NOT IN "
        "(SELECT id FROM history ORDER BY id DESC LIMIT ?)"));
    q.addBindValue(limit_);
    q.exec();
}

QVector<HistoryItem> HistoryStore::recent(int limit) {
    QVector<HistoryItem> items;
    if (!db_.isOpen()) return items;

    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "SELECT id, created_at, text FROM history ORDER BY id DESC LIMIT ?"));
    q.addBindValue(limit);
    if (!q.exec()) return items;

    while (q.next()) {
        HistoryItem item;
        item.id = q.value(0).toLongLong();
        item.createdAt = QDateTime::fromString(q.value(1).toString(), Qt::ISODate);
        item.text = q.value(2).toString();
        items.push_back(item);
    }
    return items;
}

bool HistoryStore::remove(qint64 id) {
    if (!db_.isOpen()) return false;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("DELETE FROM history WHERE id = ?"));
    q.addBindValue(id);
    return q.exec();
}

bool HistoryStore::clear() {
    if (!db_.isOpen()) return false;
    QSqlQuery q(db_);
    return q.exec(QStringLiteral("DELETE FROM history"));
}

HistoryDialog::HistoryDialog(HistoryStore* store, QWidget* parent)
    : QDialog(parent), store_(store) {
    setWindowTitle(QStringLiteral("OCR 历史"));
    resize(860, 540);

    list_ = new QListWidget(this);
    list_->setMinimumWidth(230);
    preview_ = new QPlainTextEdit(this);
    preview_->setReadOnly(true);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(list_);
    splitter->addWidget(preview_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    copyButton_ = new QPushButton(QStringLiteral("复制"), this);
    deleteButton_ = new QPushButton(QStringLiteral("删除"), this);
    auto* clearButton = new QPushButton(QStringLiteral("清空"), this);
    auto* closeButton = new QPushButton(QStringLiteral("关闭"), this);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(copyButton_);
    buttons->addWidget(deleteButton_);
    buttons->addWidget(clearButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);
    layout->addLayout(buttons);

    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        preview_->setPlainText(current ? current->data(Qt::UserRole + 1).toString() : QString());
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        copyCurrent();
    });
    connect(copyButton_, &QPushButton::clicked, this, [this] { copyCurrent(); });
    connect(deleteButton_, &QPushButton::clicked, this, [this] {
        auto* item = list_->currentItem();
        if (!item) return;
        store_->remove(item->data(Qt::UserRole).toLongLong());
        reload();
    });
    connect(clearButton, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("清空历史"),
                                  QStringLiteral("确定删除全部历史记录吗？")) != QMessageBox::Yes) {
            return;
        }
        store_->clear();
        reload();
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    reload();
}

void HistoryDialog::reload() {
    list_->clear();
    preview_->clear();
    const auto items = store_->recent();
    for (const auto& item : items) {
        QString firstLine = item.text.section(QLatin1Char('\n'), 0, 0).trimmed();
        if (firstLine.size() > 42) firstLine = firstLine.left(42) + QStringLiteral("…");
        const QString label = QStringLiteral("%1  %2")
                                  .arg(item.createdAt.toLocalTime().toString(QStringLiteral("MM-dd HH:mm")),
                                       firstLine);
        auto* row = new QListWidgetItem(label, list_);
        row->setData(Qt::UserRole, item.id);
        row->setData(Qt::UserRole + 1, item.text);
        row->setToolTip(item.text.left(500));
    }
    const bool hasItems = list_->count() > 0;
    copyButton_->setEnabled(hasItems);
    deleteButton_->setEnabled(hasItems);
}

void HistoryDialog::copyCurrent() {
    auto* item = list_->currentItem();
    if (!item) return;
    QGuiApplication::clipboard()->setText(item->data(Qt::UserRole + 1).toString());
}
