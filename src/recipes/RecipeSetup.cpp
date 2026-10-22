// SPDX-License-Identifier: GPL-3.0-only

#include "RecipeSetup.h"

#include "DeveloperEnvironment.h"
#include "toolchains/QtRepository.h"
#include "toolchains/Toolchains.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>

namespace {

// The folder of `tool` in `toolchains`, empty when it is not there.
QString homeOf(const ToolRequirement &tool, const Toolchains &toolchains)
{
    if (tool.tool == QLatin1String(QtRepository::toolId))
        return toolchains.qtFolder(tool.version, tool.kit, tool.modules);
    // One of the PATH stands for it, and has no folder to add.
    if (tool.download)
        return toolchains.isInstalled(tool.tool, tool.version) ? toolchains.installFolder(tool.tool, tool.version)
                                                               : QString();
    const std::optional<RuntimeInstall> runtime = toolchains.runtime(tool.tool, tool.version);
    return runtime ? runtime->home : QString();
}

void addRequirement(EnvironmentAdditions &additions, const RecipeRequirement &requirement, const QString &home)
{
    const QLatin1String placeholder(Recipes::homePlaceholder);
    for (QString folder : requirement.pathFolders) {
        folder.replace(placeholder, home);
        if (QFileInfo(folder).isDir())
            additions.pathFolders.append(folder);
    }
    for (auto it = requirement.variables.cbegin(); it != requirement.variables.cend(); ++it)
        additions.variables.insert(it.key(), QString(it.value()).replace(placeholder, QDir::toNativeSeparators(home)));
}

} // namespace

namespace RecipeSetup {

EnvironmentAdditions environmentAdditions(const RecipeEnvironment &environment, QStringList *problems)
{
    EnvironmentAdditions additions;
    if (environment.visualStudio) {
        QString error;
        if (const std::optional<QMap<QString, QString>> variables = DeveloperEnvironment::visualStudio(&error))
            additions.variables = *variables;
        else
            problems->append(error);
    }
    for (auto it = environment.variables.cbegin(); it != environment.variables.cend(); ++it)
        additions.variables.insert(it.key(), it.value());
    additions.pathFolders = environment.pathFolders;
    return additions;
}

EnvironmentAdditions requirementAdditions(const QList<RecipeRequirement> &requirements, const Toolchains &toolchains)
{
    EnvironmentAdditions additions;
    for (const RecipeRequirement &requirement : requirements) {
        if (const QString home = homeOf(requirement.tool, toolchains); !home.isEmpty())
            addRequirement(additions, requirement, home);
    }
    return additions;
}

} // namespace RecipeSetup
