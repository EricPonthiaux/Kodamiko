#include "GitCommands.h"

#include "FileChange.h"

GitCommands::GitCommands(QObject *parent)
    : QObject(parent)
{
    pool_.setMaxThreadCount(1);
}

GitCommands::~GitCommands()
{
    pool_.waitForDone();
}

void GitCommands::run(const QString &title, Job job, std::function<void()> onSuccess)
{
    if (pending_++ == 0)
        emit busyChanged(true);
    start(std::move(job), [this, title, onSuccess = std::move(onSuccess)](const QString &message,
                                                                          const QString &error) {
        if (error.isEmpty()) {
            if (onSuccess)
                onSuccess();
            emit succeeded(title, message);
        } else {
            emit failed(title, error);
        }
        if (--pending_ == 0)
            emit busyChanged(false);
    });
}

void GitCommands::runQuietly(Job job, std::function<void(const QString &message, const QString &error)> done)
{
    start(std::move(job), std::move(done));
}

void GitCommands::start(Job job, std::function<void(const QString &message, const QString &error)> done)
{
    pool_.start([this, job = std::move(job), done = std::move(done)] {
        QString message;
        QString error;
        try {
            message = job();
        } catch (const GitError &exception) {
            error = QString::fromStdString(exception.what());
        }
        // The result is handed back to the UI thread; it is lost if the
        // object was destroyed in the meantime.
        QMetaObject::invokeMethod(
            this, [message, error, done] { done(message, error); }, Qt::QueuedConnection);
    });
}
