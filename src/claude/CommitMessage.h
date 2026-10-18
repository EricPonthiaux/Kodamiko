#pragma once

#include "git/RepositoryStatus.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <optional>

class QProcess;
class QTimer;

// Commit message written by Claude from the changes about to be committed,
// through a one-off `claude --print` run: it works with the user's own
// Claude Code login (subscription or API key), as in a console.
namespace CommitMessage {

// What the commit will contain.
struct Changes {
    QStringList recentSubjects; // most recent first
    QByteArray stat;            // "git diff --stat"
    QByteArray diff;
    QStringList untracked; // files the commit will add, not in `diff`
};

// Reads the changes of the next commit: the index, or with `amend` the index
// against the parent of HEAD, or with `stageEverything` (nothing staged yet:
// the commit will stage `pathspec` first) every change under `pathspec`.
// Synchronous; throws GitError.
Changes read(const RepositoryInfo &repository, bool amend, bool stageEverything, const QString &pathspec);

// Instructions given to Claude as its system prompt.
QString systemPrompt();

// Standard input of `claude --print`: the recent subjects, then the changes,
// the diff cut after `diffLimit` bytes (the stat still lists every file).
QByteArray input(const Changes &changes, int diffLimit = 100'000);

// The message in the JSON printed by `claude --print --output-format json`,
// without the fences or quotes Claude may add; nullopt with `error` set if
// Claude failed (not logged in, usage limit…) or replied nothing.
std::optional<QString> parseReply(const QByteArray &json, QString *error);

} // namespace CommitMessage

// Runs one generation at a time, off the UI thread, and reports it with
// finished() or failed(); start() again or cancel() drops the one running.
class CommitMessageGenerator : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(CommitMessageGenerator)

public:
    explicit CommitMessageGenerator(QObject *parent = nullptr);
    ~CommitMessageGenerator() override;

    void start(const RepositoryInfo &repository, bool amend, bool stageEverything, const QString &pathspec);
    void cancel();

    bool isRunning() const { return running_; }

signals:
    void runningChanged(bool running);
    void finished(const QString &message);
    void failed(const QString &error);

private:
    void onChangesRead(const std::optional<CommitMessage::Changes> &changes,
                       const QString &error,
                       const QString &workDir,
                       int generation);
    void onProcessFinished(int generation);
    void fail(const QString &error);
    void setRunning(bool running);

    QThreadPool pool_;
    QProcess *process_ = nullptr;
    QTimer *timeout_;
    int generation_ = 0;
    bool running_ = false;
};
