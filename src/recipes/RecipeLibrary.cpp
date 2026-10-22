// SPDX-License-Identifier: GPL-3.0-only

#include "RecipeLibrary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QStandardPaths>

#include <algorithm>
#include <iterator>

namespace {

QString builtInFolder()
{
    // The recipes of a source tree, for their authors and the tests.
    if (const QString folder = qEnvironmentVariable("DUET_RECIPES_DIR"); !folder.isEmpty())
        return folder;
    const QDir appDir(QCoreApplication::applicationDirPath());
    for (const QString &candidate :
         {QStringLiteral("recipes"), QStringLiteral("../share/duet/recipes"), QStringLiteral("../Resources/recipes")}) {
        const QString path = QDir::cleanPath(appDir.filePath(candidate));
        if (QFileInfo(path).isDir())
            return path;
    }
    return {};
}

// The recipes of `sources`, by id, the later folders' replacing the
// earlier ones'.
QMap<QString, Recipe> recipesById(const RecipeSources &sources, QStringList *problems)
{
    QMap<QString, Recipe> byId;
    for (const QString &root : {sources.builtIn, sources.user, sources.project}) {
        if (root.isEmpty())
            continue;
        const QDir dir(root);
        for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            QString error;
            if (std::optional<Recipe> recipe = Recipes::load(dir.filePath(name), &error, problems))
                byId.insert(recipe->id, *recipe);
            else if (problems)
                problems->append(error);
        }
    }
    return byId;
}

} // namespace

namespace RecipeLibrary {

RecipeSources defaultSources(const QString &projectFolder)
{
    RecipeSources sources;
    sources.builtIn = builtInFolder();
    sources.user = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/recipes");
    if (!projectFolder.isEmpty())
        sources.project = QDir(projectFolder).filePath(QStringLiteral(".duet/recipes"));
    return sources;
}

QList<Recipe> available(const RecipeSources &sources, const QString &system, QStringList *problems)
{
    QList<Recipe> recipes;
    for (const Recipe &recipe : recipesById(sources, problems)) {
        if (recipe.isFor(system))
            recipes.append(recipe);
    }
    std::ranges::sort(recipes, [](const Recipe &a, const Recipe &b) {
        const bool aIsEmpty = a.id == QLatin1String(emptyId);
        const bool bIsEmpty = b.id == QLatin1String(emptyId);
        if (aIsEmpty != bIsEmpty)
            return aIsEmpty;
        return a.title.localeAwareCompare(b.title) < 0;
    });
    return recipes;
}

std::optional<Recipe> find(const RecipeSources &sources, const QString &id)
{
    if (id.isEmpty())
        return std::nullopt;
    const QMap<QString, Recipe> byId = recipesById(sources, nullptr);
    const auto found = byId.constFind(id);
    if (found == byId.cend())
        return std::nullopt;
    return *found;
}

QList<Recipe> forLanguage(const QList<Recipe> &recipes, const QString &family)
{
    QList<Recipe> found;
    std::ranges::copy_if(recipes, std::back_inserter(found),
                         [&family](const Recipe &recipe) { return recipe.language == family; });
    return found;
}

QList<Recipe> recipesOf(const ProjectConfig &project, QStringList *missing)
{
    const QMap<QString, Recipe> byId = recipesById(defaultSources(project.path), nullptr);
    QList<Recipe> recipes;
    for (const QString &id : project.recipes()) {
        if (const auto found = byId.constFind(id); found != byId.cend())
            recipes.append(*found);
        else if (missing)
            missing->append(id);
    }
    return recipes;
}

QList<ToolRequirement> requirementsOf(const ProjectConfig &project)
{
    QList<ToolRequirement> requirements;
    for (const Recipe &recipe : recipesOf(project))
        requirements.append(recipe.tools());
    return requirements;
}

} // namespace RecipeLibrary
