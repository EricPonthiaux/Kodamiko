#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

// What the user chose in the completion list, per project: candidates
// chosen often and lately rank higher, those erased right after being
// inserted rank lower. Kept in a small JSON file, written shortly after a
// change.
class CompletionHistory : public QObject {
    Q_OBJECT

public:
    explicit CompletionHistory(QObject *parent = nullptr);
    // Writes what is pending.
    ~CompletionHistory() override;

    // The file of the open project's history; empty: in memory only.
    void setFile(const QString &path);

    QString file() const { return file_; }

    // Where the history of `folder` is kept, among the application's data.
    static QString fileFor(const QString &folder);

    void recordAccepted(const QString &languageId,
                        const QString &label,
                        const QDateTime &when = QDateTime::currentDateTime());
    // Inserted, then erased at once.
    void recordRejected(const QString &languageId, const QString &label);

    // Added to a candidate's rank: up to +14 for a frequent and recent
    // choice, down to -9 for one erased each time.
    int boost(const QString &languageId,
              const QString &label,
              const QDateTime &now = QDateTime::currentDateTime()) const;

    void save();

private:
    struct Use {
        int accepted = 0;
        int rejected = 0;
        qint64 lastUsed = 0; // seconds since the epoch
    };

    static QString keyOf(const QString &languageId, const QString &label);
    void load();
    void changed();

    QString file_;
    QHash<QString, Use> uses_;
    QTimer saveTimer_;
};
