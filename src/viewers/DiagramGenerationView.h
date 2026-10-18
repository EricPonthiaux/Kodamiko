#pragma once

#include "claude/HookServer.h"

#include <QElapsedTimer>
#include <QWidget>

class QLabel;
class QProcess;
class QPushButton;
class QTimer;

// The tab shown while Claude draws a diagram (see DiagramRequest): what was
// asked, what Claude is doing, the time spent, and Cancel. Once the file is
// written, finished() hands over to the diagram editor; on failure, the
// error stays with Try Again and Close.
class DiagramGenerationView : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DiagramGenerationView)

public:
    // Claude draws a `kindTitle` in `path`, inside `workDir`, showing
    // `description`.
    DiagramGenerationView(QString workDir,
                          QString path,
                          QString kindTitle,
                          QString description,
                          QWidget *parent = nullptr);
    ~DiagramGenerationView() override;

    QString filePath() const { return path_; }

    void start();

signals:
    // The diagram is written; `summary` is Claude's answer.
    void finished(const QString &path, const QString &summary);
    // Cancel or Close.
    void closeRequested();

private:
    void stop();
    void readOutput();
    void onProcessFinished();
    void onHookReceived(const HookEvent &event);
    void addSteps(const QStringList &steps);
    void fail(const QString &error);
    void updateElapsed();

    QString workDir_;
    QString path_;
    QString kindTitle_;
    QString description_;

    HookServer hookServer_;
    QProcess *process_ = nullptr;
    QByteArray pending_; // output not yet split into lines
    QString answer_;     // Claude's final answer
    bool answerFailed_ = false;
    QStringList steps_;
    QElapsedTimer elapsed_;
    QTimer *clock_;

    QWidget *spinner_;
    QLabel *title_;
    QLabel *stepsLabel_;
    QLabel *elapsedLabel_;
    QLabel *error_;
    QPushButton *cancel_;
    QPushButton *retry_;
    QPushButton *close_;
};
