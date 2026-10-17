#pragma once

#include <QDateTime>
#include <QDialog>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

class QListWidget;
class QPlainTextEdit;
class QPushButton;

struct HistoryItem {
    qint64 id = 0;
    QDateTime createdAt;
    QString text;
};

class HistoryStore {
public:
    bool open(QString* error);
    bool isOpen() const { return db_.isOpen(); }
    bool add(const QString& text);
    QVector<HistoryItem> recent(int limit = 200);
    bool remove(qint64 id);
    bool clear();
    void setLimit(int limit);

private:
    void prune();
    QSqlDatabase db_;
    int limit_ = 500;
};

class HistoryDialog : public QDialog {
public:
    explicit HistoryDialog(HistoryStore* store, QWidget* parent = nullptr);

private:
    void reload();
    void copyCurrent();

    HistoryStore* store_;
    QListWidget* list_;
    QPlainTextEdit* preview_;
    QPushButton* copyButton_;
    QPushButton* deleteButton_;
};
