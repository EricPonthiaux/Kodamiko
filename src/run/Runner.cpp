#include "Runner.h"

#include "BuildOutputParser.h"
#include "diagnostics/ProblemStore.h"
#include "terminal/TerminalWidget.h"
#include "toolchains/Toolchains.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace {

constexpr int publishDelayMs = 300;
// The exit code shells give a command they cannot run.
constexpr int notStartedExitCode = 127;

const QString selectedKey = QStringLiteral("run/selected");

} // namespace

Runner::Runner(ProblemStore *problems, QObject *parent)
    : QObject(parent)
    , problems_(problems)
{
}

Runner::~Runner() = default;

void Runner::setProject(const ProjectConfig &project, const Toolchains *toolchains)
{
    project_ = project;
    toolchains_ = toolchains;
    emit selectionChanged();
}

QList<RunConfiguration> Runner::configurations(QString *error) const
{
    if (project_.path.isEmpty())
        return {};
    return RunConfigurations::all(project_.path, error);
}

std::optional<RunConfiguration> Runner::selected() const
{
    const QList<RunConfiguration> all = configurations();
    if (all.isEmpty())
        return std::nullopt;
    const QString name = QSettings().value(selectedKey).toMap().value(project_.path).toString();
    for (const RunConfiguration &configuration : all) {
        if (configuration.name == name)
            return configuration;
    }
    return all.first();
}

void Runner::select(const QString &name)
{
    QSettings settings;
    QVariantMap selected = settings.value(selectedKey).toMap();
    selected.insert(project_.path, name);
    settings.setValue(selectedKey, selected);
    emit selectionChanged();
}

std::optional<RunConfiguration> Runner::buildConfiguration() const
{
    if (const std::optional<RunConfiguration> chosen = selected();
        chosen && chosen->kind == RunConfiguration::Kind::Build)
        return chosen;
    for (const RunConfiguration &configuration : configurations()) {
        if (configuration.kind == RunConfiguration::Kind::Build)
            return configuration;
    }
    return std::nullopt;
}

ProcessSpec Runner::processSpec(const RunConfiguration &configuration) const
{
    ProcessSpec spec;
    spec.workingDirectory = QDir::cleanPath(QDir(project_.path).filePath(configuration.folder));
    spec.environment = terminalEnvironment();
    const auto prependPath = [&spec](const QString &folder) {
        spec.environment.insert(QStringLiteral("PATH"), QDir::toNativeSeparators(folder) + QDir::listSeparator() +
                                                            spec.environment.value(QStringLiteral("PATH")));
    };
    // The runtimes the project was set up with come first.
    if (toolchains_) {
        for (const LanguageSetup &setup : project_.languages) {
            if (!setup.enabled)
                continue;
            const std::optional<RuntimeInstall> runtime = toolchains_->runtimeFor(setup);
            if (!runtime)
                continue;
            if (runtime->tool == QLatin1String("jdk")) {
                spec.environment.insert(QStringLiteral("JAVA_HOME"), QDir::toNativeSeparators(runtime->home));
                prependPath(runtime->home + QStringLiteral("/bin"));
            } else if (!runtime->executable.isEmpty()) {
                const QString bin = QFileInfo(runtime->executable).absolutePath();
                prependPath(bin);
                // A virtual environment, as its activate script sets it.
                if (runtime->tool == QLatin1String("python") &&
                    QFileInfo::exists(bin + QStringLiteral("/../pyvenv.cfg")))
                    spec.environment.insert(QStringLiteral("VIRTUAL_ENV"),
                                            QDir::toNativeSeparators(QDir::cleanPath(bin + QStringLiteral("/.."))));
            }
        }
    }
    for (auto it = configuration.environment.cbegin(); it != configuration.environment.cend(); ++it)
        spec.environment.insert(it.key(), it.value());
#ifdef Q_OS_WIN
    spec.program = qEnvironmentVariable("ComSpec", QStringLiteral("cmd.exe"));
    spec.arguments = {QStringLiteral("/d"), QStringLiteral("/s"), QStringLiteral("/c"), configuration.command};
#else
    // A login shell, as in Duet's terminals: the PATH of the user's profile.
    spec.program = qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
    spec.arguments = {QStringLiteral("-l"), QStringLiteral("-c"), configuration.command};
#endif
    return spec;
}

void Runner::start(const RunConfiguration &configuration, TerminalWidget *terminal)
{
    const QString name = configuration.name;
    if (const std::shared_ptr<Run> previous = runs_.value(name);
        previous && previous->terminal && previous->terminal != terminal && previous->terminal->isRunning()) {
        previous->terminal->terminate();
    }
    const ProcessSpec spec = processSpec(configuration);
    const auto run = std::make_shared<Run>();
    run->configuration = configuration;
    run->terminal = terminal;
    run->parser = std::make_unique<BuildOutputParser>(spec.workingDirectory, project_.path);
    run->publishTimer = new QTimer(terminal);
    run->publishTimer->setSingleShot(true);
    run->publishTimer->setInterval(publishDelayMs);
    runs_.insert(name, run);
    order_.removeAll(name);
    order_.prepend(name);
    problems_->setBuildProblems(name, {});

    const std::weak_ptr<Run> weak = run;
    connect(run->publishTimer, &QTimer::timeout, this, [this, weak] {
        if (const std::shared_ptr<Run> run = weak.lock())
            publish(*run);
    });
    connect(terminal, &TerminalWidget::outputReceived, this, [weak](const QByteArray &data) {
        const std::shared_ptr<Run> run = weak.lock();
        if (!run)
            return;
        run->parser->feed(data);
        // Problems shown while the build goes on.
        if (run->parser->problems().size() > run->published && !run->publishTimer->isActive())
            run->publishTimer->start();
    });
    connect(terminal, &TerminalWidget::finished, this,
            [this, name, terminal](int exitCode) { onFinished(name, terminal, exitCode); });

    // Emitted once the process runs, so isRunning() is already true.
    if (terminal->start(spec))
        emit started(configuration);
    else
        onFinished(name, terminal, notStartedExitCode);
}

void Runner::publish(Run &run)
{
    run.published = run.parser->problems().size();
    problems_->setBuildProblems(run.configuration.name, run.parser->problems());
}

void Runner::onFinished(const QString &name, TerminalWidget *terminal, int exitCode)
{
    const std::shared_ptr<Run> run = runs_.value(name);
    if (!run || run->terminal != terminal)
        return; // a run since replaced
    run->parser->finish();
    if (run->publishTimer)
        run->publishTimer->stop();
    publish(*run);
    int errors = 0;
    int warnings = 0;
    for (const Problem &problem : run->parser->problems())
        (problem.severity == Diagnostic::Severity::Error ? errors : warnings) += 1;
    emit finished(run->configuration, exitCode, errors, warnings);
}

TerminalWidget *Runner::terminal(const QString &name) const
{
    const std::shared_ptr<Run> run = runs_.value(name);
    return run ? run->terminal.data() : nullptr;
}

bool Runner::isRunning(const QString &name) const
{
    const TerminalWidget *widget = terminal(name);
    return widget && widget->isRunning();
}

QStringList Runner::running() const
{
    QStringList names;
    for (const QString &name : order_) {
        if (isRunning(name))
            names.append(name);
    }
    return names;
}

void Runner::stop(const QString &name)
{
    const std::shared_ptr<Run> run = runs_.value(name);
    if (!run || !run->terminal || !run->terminal->isRunning())
        return;
    if (run->interrupted) {
        run->terminal->terminate();
        return;
    }
    run->interrupted = true;
    run->terminal->interrupt();
}
