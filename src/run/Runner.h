#pragma once

#include "RunConfiguration.h"
#include "projects/ProjectConfig.h"
#include "terminal/ProcessSpec.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <memory>
#include <optional>

class BuildOutputParser;
class ProblemStore;
class TerminalWidget;
class Toolchains;

// Runs the project's configurations, each in a terminal of its own, and
// gives the problems read in its output to the ProblemStore (under the
// configuration's name), as they come. Remembers the configuration chosen
// for each project.
class Runner : public QObject {
    Q_OBJECT

public:
    explicit Runner(ProblemStore *problems, QObject *parent = nullptr);
    ~Runner() override;

    // The project's folder and its runtimes (JDK, Node, Python), which the
    // commands find first in their PATH.
    void setProject(const ProjectConfig &project, const Toolchains *toolchains);

    QString folder() const { return project_.path; }

    // Read again from the project's files each time.
    QList<RunConfiguration> configurations(QString *error = nullptr) const;
    // The chosen configuration, or the first one.
    std::optional<RunConfiguration> selected() const;
    void select(const QString &name);
    // The chosen configuration if it builds, else the first that does.
    std::optional<RunConfiguration> buildConfiguration() const;

    // The shell running `configuration`'s command in its folder.
    ProcessSpec processSpec(const RunConfiguration &configuration) const;

    // Starts `configuration` in `terminal`, which the caller shows.
    void start(const RunConfiguration &configuration, TerminalWidget *terminal);
    // The terminal of the last run of `name`, while it is open.
    TerminalWidget *terminal(const QString &name) const;
    bool isRunning(const QString &name) const;
    // The configurations running, the last started first.
    QStringList running() const;
    // Asks `name` to stop (Ctrl+C); the second time, ends it.
    void stop(const QString &name);

signals:
    void selectionChanged();
    void started(const RunConfiguration &configuration);
    // `exitCode` -1: stopped.
    void finished(const RunConfiguration &configuration, int exitCode, int errors, int warnings);

private:
    struct Run {
        RunConfiguration configuration;
        QPointer<TerminalWidget> terminal;
        std::unique_ptr<BuildOutputParser> parser;
        qsizetype published = 0; // problems given to the store
        bool interrupted = false;
        QPointer<QTimer> publishTimer;
    };

    void publish(Run &run);
    void onFinished(const QString &name, TerminalWidget *terminal, int exitCode);

    ProblemStore *problems_;
    ProjectConfig project_;
    const Toolchains *toolchains_ = nullptr;
    QHash<QString, std::shared_ptr<Run>> runs_;
    QStringList order_; // names, the last started first
};
