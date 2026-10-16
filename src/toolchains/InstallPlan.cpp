#include "InstallPlan.h"

#include "ToolCatalog.h"
#include "Toolchains.h"

#include <QObject>

#include <algorithm>

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
    const QString preferred = ToolCatalog::defaultRuntimeVersion(spec->family);
    if (preferred.toInt() >= minimum)
        return preferred;
    for (auto it = spec->versions.crbegin(); it != spec->versions.crend(); ++it) {
        if (it->toInt() >= minimum)
            return *it;
    }
    return spec->versions.value(0);
}

bool isSystem(const QString &version)
{
    return version.isEmpty() || version == QLatin1String(ProjectLanguages::systemVersion);
}

} // namespace

namespace InstallPlan {

QList<InstallStep> plan(const ProjectConfig &config, const Toolchains &toolchains)
{
    QList<InstallStep> steps;
    const auto add = [&steps](const QString &tool, const QString &version, const QString &reason) {
        for (const InstallStep &step : std::as_const(steps)) {
            if (step.tool == tool && step.version == version)
                return;
        }
        steps.append({tool, version, titleOf(tool, version), reason});
    };
    const auto planned = [&steps](const QString &tool, int minimum) {
        return std::any_of(steps.cbegin(), steps.cend(), [&](const InstallStep &step) {
            return step.tool == tool && step.version.toInt() >= minimum;
        });
    };

    for (const LanguageSetup &setup : config.languages) {
        if (!setup.enabled)
            continue;
        const QString family = ProjectLanguages::title(setup.language);
        const QString runtime = ToolCatalog::runtimeOf(setup.language);
        if (!runtime.isEmpty() && !setup.runtimeVersion.isEmpty() && !toolchains.runtimeFor(setup))
            add(runtime, setup.runtimeVersion, QObject::tr("%1 runtime").arg(family));

        const ToolSpec *server = ToolCatalog::find(setup.server);
        if (!server)
            continue;
        const bool system = isSystem(setup.serverVersion);
        // What it runs on: always for jdtls (Duet starts it with Java),
        // for Duet's own npm installs otherwise.
        if (!server->runtime.isEmpty() && (!system || server->runtime == QLatin1String("jdk"))
            && !toolchains.runtimeAtLeast(server->runtime, server->minimumRuntime)
            && !planned(server->runtime, server->minimumRuntime))
            add(server->runtime, runtimeVersionFor(server->runtime, server->minimumRuntime),
                QObject::tr("runs %1").arg(server->id));
        if (!system && !toolchains.isInstalled(server->id, setup.serverVersion))
            add(server->id, setup.serverVersion, QObject::tr("%1 language server").arg(family));
    }
    // The runtimes first: the servers need them to install and run.
    std::stable_partition(steps.begin(), steps.end(), [](const InstallStep &step) {
        const ToolSpec *spec = ToolCatalog::find(step.tool);
        return spec && spec->isRuntime;
    });
    return steps;
}

QStringList unavailable(const ProjectConfig &config, const Toolchains &toolchains)
{
    QStringList problems;
    for (const LanguageSetup &setup : config.languages) {
        if (!setup.enabled)
            continue;
        if (setup.language == QLatin1String("python") && !toolchains.runtimeFor(setup))
            problems.append(setup.runtimeVersion.isEmpty()
                                ? QObject::tr("No Python found: install one, Duet does not download it.")
                                : QObject::tr("No Python %1 found: install it, Duet does not download it.")
                                      .arg(setup.runtimeVersion));
        if (!setup.server.isEmpty() && isSystem(setup.serverVersion)
            && toolchains.systemProgram(setup.server).isEmpty())
            problems.append(QObject::tr("%1 is not in the PATH: choose a version for Duet to download.")
                                .arg(setup.server));
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
