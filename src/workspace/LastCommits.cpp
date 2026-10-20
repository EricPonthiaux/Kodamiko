#include "LastCommits.h"

#include "git/CommitLog.h"
#include "git/FileChange.h"

#include <QDir>
#include <QLocale>

namespace {

QString readLastCommit(const QString &folder, const QString &path)
{
    try {
        const std::optional<CommitLog> log =
            CommitLog::read(folder, {.limit = 1, .path = QDir(folder).relativeFilePath(path)});
        if (!log || log->commits.isEmpty())
            return {};
        const CommitSummary &commit = log->commits.first();
        return QStringLiteral("%1 %2 (%3, %4)")
            .arg(commit.shortId, commit.summary, commit.author,
                 QLocale().toString(commit.date.date(), QLocale::ShortFormat));
    } catch (const GitError &) {
        return {}; // the tooltip goes without it
    }
}

} // namespace

LastCommits::LastCommits(QObject *parent)
    : QObject(parent)
{
    pool_.setMaxThreadCount(1);
}

LastCommits::~LastCommits()
{
    pool_.waitForDone();
}

void LastCommits::setFolder(const QString &folder)
{
    folder_ = folder;
}

void LastCommits::request(const QString &path)
{
    if (folder_.isEmpty() || pending_.contains(path))
        return;
    pending_.insert(path);
    pool_.start([this, folder = folder_, path] {
        QString description = readLastCommit(folder, path);
        QMetaObject::invokeMethod(
            this,
            [this, path, description = std::move(description)] {
                pending_.remove(path);
                emit found(path, description);
            },
            Qt::QueuedConnection);
    });
}
