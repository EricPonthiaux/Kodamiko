// SPDX-License-Identifier: GPL-3.0-only

#include "RecipeServers.h"

#include "toolchains/Toolchains.h"

#include <QDir>
#include <QJsonArray>

#include <algorithm>

namespace {

// The folder of the requirement of `recipe` that installs `tool`, empty
// when it is not installed.
QString toolFolder(const Recipe &recipe, const QString &tool, const Toolchains &toolchains)
{
    const auto requirement =
        std::ranges::find(recipe.requirements, tool, [](const RecipeRequirement &entry) { return entry.tool.tool; });
    if (requirement == recipe.requirements.end())
        return {};
    const QString version = requirement->tool.version;
    return toolchains.isInstalled(tool, version) ? toolchains.installFolder(tool, version) : QString();
}

QString withFolders(QString text, const QString &home, const QString &projectFolder)
{
    return text.replace(QLatin1String(Recipes::homePlaceholder), home)
        .replace(QLatin1String(Recipes::projectPlaceholder), projectFolder);
}

QJsonValue filled(const QJsonValue &json, const QString &projectFolder)
{
    if (json.isString())
        return json.toString().replace(QLatin1String(Recipes::projectPlaceholder), projectFolder);
    if (json.isArray()) {
        QJsonArray array;
        for (const QJsonValue &item : json.toArray())
            array.append(filled(item, projectFolder));
        return array;
    }
    if (!json.isObject())
        return json;
    QJsonObject object = json.toObject();
    for (auto it = object.begin(); it != object.end(); ++it)
        it.value() = filled(QJsonValue(it.value()), projectFolder);
    return object;
}

std::optional<LanguageServerSpec> specOf(const Recipe &recipe,
                                         const RecipeLanguageServer &server,
                                         const QString &projectFolder,
                                         const Toolchains &toolchains)
{
    const QString home = toolFolder(recipe, server.tool, toolchains);
    if (home.isEmpty())
        return std::nullopt;
    QStringList arguments;
    for (const QString &argument : server.arguments)
        arguments.append(withFolders(argument, home, projectFolder));
    return LanguageServerSpec{.name = server.name,
                              .program = QDir(home).filePath(server.script),
                              .arguments = arguments,
                              .languages = server.languages,
                              .triggerCharacters = server.triggerCharacters,
                              .isNodeScript = true};
}

} // namespace

namespace RecipeServers {

LanguageServers::Additions additionsOf(const QList<Recipe> &recipes,
                                       const QString &projectFolder,
                                       const Toolchains &toolchains)
{
    const QString folder = QDir(projectFolder).absolutePath();
    LanguageServers::Additions additions;
    for (const Recipe &recipe : recipes) {
        for (const RecipeLanguageServer &server : recipe.languageServers) {
            if (const std::optional<LanguageServerSpec> spec = specOf(recipe, server, folder, toolchains))
                additions.servers.append(*spec);
        }
        for (auto it = recipe.serverOptions.cbegin(); it != recipe.serverOptions.cend(); ++it) {
            const LanguageServers::Options added{.initialization = filled(it->initialization, folder).toObject(),
                                                 .settings = filled(it->settings, folder).toObject()};
            additions.options.insert(it.key(), LanguageServers::merged(additions.options.value(it.key()), added));
        }
        for (auto it = recipe.servers.cbegin(); it != recipe.servers.cend(); ++it) {
            if (!it->runtime.tool.isEmpty())
                additions.runtimes.insert(it.key(), it->runtime);
        }
    }
    return additions;
}

} // namespace RecipeServers
