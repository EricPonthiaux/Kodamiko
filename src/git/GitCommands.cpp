#include "GitCommands.h"

GitCommands::GitCommands(QObject *parent)
    : QObject(parent)
{
    pool_.setMaxThreadCount(1);
}

GitCommands::~GitCommands()
{
    cancelled_ = true;
    pool_.waitForDone();
}

void GitCommands::run(const QString &title, Job job, std::function<void()> onSuccess, FailureHandler onFailure)
{
    if (pending_++ == 0)
        emit busyChanged(true);
    start(std::move(job), finishing(title, std::move(onSuccess), std::move(onFailure)));
}

void GitCommands::runLong(const QString &title, LongJob job, std::function<void()> onSuccess, FailureHandler onFailure)
{
    if (pending_++ == 0)
        emit busyChanged(true);
    start(
        [this, title, job = std::move(job), feedback = feedback(title, true)] {
            // Cancelling stops the operation running then, not the ones
            // queued after it.
            cancelled_ = false;
            QMetaObject::invokeMethod(this, [this, title] { emit progress(title, {}); }, Qt::QueuedConnection);
            return job(feedback);
        },
        finishing(title, std::move(onSuccess), std::move(onFailure)));
}

void GitCommands::runQuietly(Job job, std::function<void(const QString &message, const QString &error)> done)
{
    start(std::move(job), [done = std::move(done)](const QString &message, const std::optional<GitError> &error) {
        done(message, error ? error->message() : QString());
    });
}

void GitCommands::runLongQuietly(LongJob job, std::function<void(const QString &message, const QString &error)> done)
{
    runQuietly([job = std::move(job), feedback = feedback({}, false)] { return job(feedback); }, std::move(done));
}

void GitCommands::cancel()
{
    cancelled_ = true;
}

GitCommands::Done GitCommands::finishing(const QString &title, std::function<void()> onSuccess,
                                         FailureHandler onFailure)
{
    return [this, title, onSuccess = std::move(onSuccess),
            onFailure = std::move(onFailure)](const QString &message, const std::optional<GitError> &error) {
        if (!error) {
            if (onSuccess)
                onSuccess();
            emit succeeded(title, message);
        } else if (!onFailure || !onFailure(*error)) {
            emit failed(title, error->message());
        }
        if (--pending_ == 0)
            emit busyChanged(false);
    };
}

Git::Feedback GitCommands::feedback(const QString &title, bool cancellable)
{
    Git::Feedback feedback;
    feedback.cancel = cancellable ? &cancelled_ : &neverCancelled_;
    feedback.environment = remoteEnvironment_;
    if (cancellable) {
        feedback.progress = [this, title](const QString &line) {
            QMetaObject::invokeMethod(this, [this, title, line] { emit progress(title, line); }, Qt::QueuedConnection);
        };
    }
    return feedback;
}

void GitCommands::start(Job job, Done done)
{
    pool_.start([this, job = std::move(job), done = std::move(done)] {
        QString message;
        std::optional<GitError> error;
        try {
            message = job();
        } catch (const GitError &exception) {
            error = exception;
        }
        // The result is handed back to the UI thread; it is lost if the
        // object was destroyed in the meantime.
        QMetaObject::invokeMethod(this, [message, error, done] { done(message, error); }, Qt::QueuedConnection);
    });
}
