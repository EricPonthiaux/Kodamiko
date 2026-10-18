#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>

// The last commit of the files of a working folder, read off the UI thread
// when asked for: walking the history down to a file changed long ago takes
// time in a large repository.
class LastCommits : public QObject {
    Q_OBJECT

public:
    explicit LastCommits(QObject *parent = nullptr);
    LastCommits(const LastCommits &) = delete;
    LastCommits &operator=(const LastCommits &) = delete;
    LastCommits(LastCommits &&) = delete;
    LastCommits &operator=(LastCommits &&) = delete;
    // Waits for the reading in progress.
    ~LastCommits() override;

    void setFolder(const QString &folder);
    // Reads the last commit of `path` (absolute), unless it is being read;
    // found() reports it.
    void request(const QString &path);

signals:
    // `description`: "a1b2c3d Fix the parser (Ada, 12/03/2026)"; empty
    // outside a repository or for a file without commits.
    void found(const QString &path, const QString &description);

private:
    QString folder_;
    QSet<QString> pending_;
    QThreadPool pool_;
};
