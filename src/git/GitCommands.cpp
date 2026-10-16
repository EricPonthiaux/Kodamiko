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
    pool_.start([this, title, job = std::move(job), onSuccess = std::move(onSuccess)] {
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
            this,
            [this, title, message, error, onSuccess] {
                if (error.isEmpty()) {
                    if (onSuccess)
                        onSuccess();
                    emit succeeded(title, message);
                } else {
                    emit failed(title, error);
                }
                if (--pending_ == 0)
                    emit busyChanged(false);
            },
            Qt::QueuedConnection);
    });
}
