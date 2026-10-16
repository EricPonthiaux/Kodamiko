#pragma once

#include <QObject>
#include <QThreadPool>

#include <functional>

// Queue of git operations that modify the repository (see GitOperations). They
// run one at a time, in order, off the UI thread: a slow commit hook does not
// freeze the editor.
class GitCommands : public QObject {
    Q_OBJECT

public:
    // Runs off the UI thread: must not touch any widget.
    // Returns the message to show on success; throws GitError.
    using Job = std::function<QString()>;

    explicit GitCommands(QObject *parent = nullptr);
    // Waits for pending operations to finish.
    ~GitCommands() override;

    // `title` describes the operation ("Commit", "Staging"…). `onSuccess`
    // runs on the UI thread if the operation succeeds.
    void run(const QString &title, Job job, std::function<void()> onSuccess = {});
    bool isBusy() const { return pending_ > 0; }

signals:
    void busyChanged(bool busy);
    void succeeded(const QString &title, const QString &message);
    void failed(const QString &title, const QString &error);

private:
    QThreadPool pool_;
    int pending_ = 0;
};
