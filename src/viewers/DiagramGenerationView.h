// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "agent/AgentRun.h"
#include "agent/HookServer.h"

#include <QElapsedTimer>
#include <QWidget>

class AgentProvider;
class QLabel;
class QPushButton;
class DiagramSketch;
class QTimer;
class StepTimeline;

// The tab shown while the agent draws a diagram (see DiagramRequest): a
// sketch that draws itself, the agent's steps, the time spent, and Cancel.
// Once the file is written, finished() hands over to the diagram editor; on
// failure, the error stays with Try Again and Close.
class DiagramGenerationView : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DiagramGenerationView)

public:
    // `agent` draws a diagram of the family `kindId` (DiagramKind::id), named
    // `kindTitle`, in `path`, inside `workDir`, that shows `description`, in
    // a file run (AgentFileRun).
    struct Request {
        QString workDir;
        QString path;
        QString kindId;
        QString kindTitle;
        QString description;
        const AgentProvider *agent = nullptr; // not owned, never null
    };

    explicit DiagramGenerationView(Request drawing, QWidget *parent = nullptr);
    ~DiagramGenerationView() override;

    QString filePath() const { return path_; }

    void start();

signals:
    // The diagram is written; `summary` is the agent's answer.
    void finished(const QString &path, const QString &summary);
    // Cancel or Close.
    void closeRequested();

private:
    void stop();
    void onRunFinished(const AgentRun::Outcome &outcome);
    void onHookReceived(const HookEvent &event);
    void fail(const QString &error);
    void updateElapsed();

    QString workDir_;
    QString path_;
    QString kindTitle_;
    QString description_;

    const AgentProvider *agent_;
    HookServer hookServer_;
    AgentRun *run_;
    QElapsedTimer elapsed_;
    QTimer *clock_;

    DiagramSketch *sketch_;
    QLabel *title_;
    StepTimeline *steps_;
    QLabel *elapsedLabel_;
    QLabel *error_;
    QPushButton *cancel_;
    QPushButton *retry_;
    QPushButton *close_;
};
