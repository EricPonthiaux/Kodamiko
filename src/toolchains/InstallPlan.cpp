// SPDX-License-Identifier: GPL-3.0-only

#include "InstallPlan.h"

#include "QtRepository.h"
#include "ToolCatalog.h"
#include "Toolchains.h"

#include <QObject>

#include <algorithm>
#include <ranges>

namespace {

QString titleOf(const QString &tool, const QString &version)
{
    const ToolSpec *spec = ToolCatalog::find(tool);
    return QStringLiteral("%1 %2").arg(spec && spec->isRuntime ? spec->title : tool, version);
}

// The version of runtime `tool` to download to run a server needing
// `minimum`: the languages' default when recent enough, else the oldest
// offered that is.
QString runtimeVersionFor(const QString &tool, int minimum)
{
    const ToolSpec *spec = ToolCatalog::find(tool);
    if (!spec)
        return {};
    QString preferred = ToolCatalog::defaultRuntimeVersion(spec->family);
    if (preferred.toInt() >= minimum)
        return preferred;
    for (const auto &version : std::views::reverse(spec->versions)) {
        if (version.toInt() >= minimum)
            return version;
    }
    return spec->versions.value(0);
}

bool isSystem(const QString &version)
{
    return version.isEmpty() || version == QLatin1String(ProjectLanguages::systemVersion);
}

// The steps of a plan, each tool and version once.
class PlannedSteps {
public:
    void add(const InstallStep &step) { steps_.append(step); }

    void add(const QString &tool, const QString &version, const QString &reason)
    {
        const bool isPlanned = std::ranges::any_of(
            steps_, [&](const InstallStep &step) { return step.tool == tool && step.version == version; });
        if (!isPlanned)
            steps_.append({tool, version, titleOf(tool, version), reason});
    }

    bool hasAtLeast(const QString &tool, int minimum) const
    {
        return std::ranges::any_of(
            steps_, [&](const InstallStep &step) { return step.tool == tool && step.version.toInt() >= minimum; });
    }

    // The runtimes first: the servers need them to install and run.
    QList<InstallStep> runtimesFirst() const
    {
        QList<InstallStep> steps = steps_;
        std::stable_partition(steps.begin(), steps.end(), [](const InstallStep &step) {
            const ToolSpec *spec = ToolCatalog::find(step.tool);
            return spec && spec->isRuntime;
        });
        return steps;
    }

private:
    QList<InstallStep> steps_;
};

// What the server runs on: always for jdtls (Duet starts it with Java), for
// Duet's own npm installs otherwise.
void planServerRuntime(const ToolSpec &server,
                       const LanguageSetup &setup,
                       const Toolchains &toolchains,
                       PlannedSteps &steps)
{
    if (server.runtime.isEmpty())
        return;
    const bool needsRuntime = !isSystem(setup.serverVersion) || server.runtime == QLatin1String("jdk");
    if (!needsRuntime)
        return;
    const bool hasRuntime = toolchains.runtimeAtLeast(server.runtime, server.minimumRuntime) ||
                            steps.hasAtLeast(server.runtime, server.minimumRuntime);
    if (!hasRuntime)
        steps.add(server.runtime, runtimeVersionFor(server.runtime, server.minimumRuntime),
                  QObject::tr("runs %1").arg(server.id));
}

void planLanguage(const LanguageSetup &setup, const Toolchains &toolchains, PlannedSteps &steps)
{
    const QString family = ProjectLanguages::title(setup.language);
    const QString runtime = ToolCatalog::runtimeOf(setup.language);
    if (!runtime.isEmpty() && !setup.runtimeVersion.isEmpty() && !toolchains.runtimeFor(setup))
        steps.add(runtime, setup.runtimeVersion, QObject::tr("%1 runtime").arg(family));

    const ToolSpec *server = ToolCatalog::find(setup.server);
    if (!server)
        return;
    const bool system = isSystem(setup.serverVersion);
    planServerRuntime(*server, setup, toolchains, steps);
    if (!system && !toolchains.isInstalled(server->id, setup.serverVersion))
        steps.add(server->id, setup.serverVersion, QObject::tr("%1 language server").arg(family));
}

// The runtime Duet downloads for `requirement`, nullptr when it offers none.
const ToolSpec *downloadableRuntime(const ToolRequirement &requirement)
{
    const ToolSpec *spec = ToolCatalog::find(requirement.tool);
    if (!spec || !spec->isRuntime)
        return nullptr;
    const bool isOffered = requirement.version.isEmpty() || spec->versions.contains(requirement.version);
    return isOffered ? spec : nullptr;
}

bool isQt(const ToolRequirement &requirement)
{
    return requirement.tool == QLatin1String(QtRepository::toolId);
}

// "Qt 6.10 msvc2022_64", "Qt msvc2022_64 with qtcharts".
QString qtTitle(const ToolRequirement &requirement)
{
    QStringList words{QStringLiteral("Qt"), requirement.version, requirement.kit};
    words.removeAll(QString());
    QString title = words.join(QLatin1Char(' '));
    if (!requirement.modules.isEmpty())
        title += QObject::tr(" with %1").arg(requirement.modules.join(QStringLiteral(", ")));
    return title;
}

void planQt(const ToolRequirement &requirement, const Toolchains &toolchains, PlannedSteps &steps)
{
    const bool isPresent = !toolchains.qtFolder(requirement.version, requirement.kit, requirement.modules).isEmpty();
    if (requirement.kit.isEmpty() || isPresent)
        return;
    steps.add({.tool = requirement.tool,
               .version = requirement.version,
               .title = qtTitle(requirement),
               .reason = QObject::tr("required by the recipe"),
               .kit = requirement.kit,
               .modules = requirement.modules,
               .source = std::nullopt,
               .check = {}});
}

// The command standing for `requirement` in the PATH.
QString programOf(const ToolRequirement &requirement)
{
    return requirement.program.isEmpty() ? requirement.tool : requirement.program;
}

void planDownload(const ToolRequirement &requirement, const Toolchains &toolchains, PlannedSteps &steps)
{
    const bool isPresent = toolchains.isInstalled(requirement.tool, requirement.version) ||
                           !toolchains.systemProgram(programOf(requirement)).isEmpty();
    if (isPresent)
        return;
    steps.add({.tool = requirement.tool,
               .version = requirement.version,
               .title = QStringLiteral("%1 %2").arg(requirement.tool, requirement.version),
               .reason = QObject::tr("required by the recipe"),
               .kit = {},
               .modules = {},
               .source = requirement.download,
               .check = requirement.check});
}

void planRequirement(const ToolRequirement &requirement, const Toolchains &toolchains, PlannedSteps &steps)
{
    if (isQt(requirement)) {
        planQt(requirement, toolchains, steps);
        return;
    }
    if (requirement.download) {
        planDownload(requirement, toolchains, steps);
        return;
    }
    const ToolSpec *spec = downloadableRuntime(requirement);
    if (!spec || toolchains.runtime(requirement.tool, requirement.version))
        return;
    const QString version =
        requirement.version.isEmpty() ? ToolCatalog::defaultRuntimeVersion(spec->family) : requirement.version;
    steps.add(requirement.tool, version, QObject::tr("required by the recipe"));
}

// Why `requirement` cannot be met, empty when it can.
QString requirementProblem(const ToolRequirement &requirement, const Toolchains &toolchains)
{
    if (isQt(requirement))
        return requirement.kit.isEmpty() ? QObject::tr("The recipe requires Qt without saying which kit.") : QString();
    if (requirement.download) {
        const QString &error = requirement.download->error;
        return error.isEmpty() ? QString()
                               : QObject::tr("The recipe cannot download %1: %2").arg(requirement.tool, error);
    }
    if (downloadableRuntime(requirement) || toolchains.runtime(requirement.tool, requirement.version))
        return {};
    if (!ToolCatalog::find(requirement.tool)) {
        const bool isInPath = !toolchains.systemProgram(programOf(requirement)).isEmpty();
        return isInPath ? QString()
                        : QObject::tr("The recipe requires %1, which is not in the PATH: install it.")
                              .arg(programOf(requirement));
    }
    const QString tool = QStringLiteral("%1 %2").arg(requirement.tool, requirement.version).trimmed();
    return QObject::tr("The recipe requires %1, which Duet cannot download: install it.").arg(tool);
}

} // namespace

namespace InstallPlan {

QList<InstallStep> plan(const ProjectConfig &config,
                        const Toolchains &toolchains,
                        const QList<ToolRequirement> &required)
{
    PlannedSteps steps;
    for (const ToolRequirement &requirement : required)
        planRequirement(requirement, toolchains, steps);
    for (const LanguageSetup &setup : config.languages) {
        if (setup.enabled)
            planLanguage(setup, toolchains, steps);
    }
    return steps.runtimesFirst();
}

QStringList unavailable(const ProjectConfig &config,
                        const Toolchains &toolchains,
                        const QList<ToolRequirement> &required)
{
    QStringList problems;
    for (const ToolRequirement &requirement : required) {
        if (const QString problem = requirementProblem(requirement, toolchains); !problem.isEmpty())
            problems.append(problem);
    }
    for (const LanguageSetup &setup : config.languages) {
        if (!setup.enabled)
            continue;
        if (setup.language == QLatin1String("python") && !toolchains.runtimeFor(setup))
            problems.append(setup.runtimeVersion.isEmpty()
                                ? QObject::tr("No Python found: install one, Duet does not download it.")
                                : QObject::tr("No Python %1 found: install it, Duet does not download it.")
                                      .arg(setup.runtimeVersion));
        if (!setup.server.isEmpty() && isSystem(setup.serverVersion) &&
            toolchains.systemProgram(setup.server).isEmpty())
            problems.append(
                QObject::tr("%1 is not in the PATH: choose a version for Duet to download.").arg(setup.server));
    }
    return problems;
}

QString summary(const QList<InstallStep> &steps)
{
    QStringList titles;
    for (const InstallStep &step : steps)
        titles.append(step.title);
    return titles.join(QStringLiteral(", "));
}

} // namespace InstallPlan
