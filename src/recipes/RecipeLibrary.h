// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Recipe.h"
#include "projects/ProjectConfig.h"
#include "system/Host.h"

#include <QList>
#include <QString>

#include <optional>

// The folders recipes are found in, each holding one folder per recipe. A
// recipe of a later folder replaces the one of an earlier folder with the
// same id: the user's replace Duet's, a project's replace the user's.
struct RecipeSources {
    QString builtIn; // shipped with Duet
    QString user;    // <application data>/recipes
    QString project; // <project>/.duet/recipes, shared with the project's repository
};

namespace RecipeLibrary {

// The "empty" recipe: no files, nothing added.
constexpr auto emptyId = "empty";

// Duet's own (next to the executable in the build, in share/duet once
// installed, in the bundle's resources on macOS; $DUET_RECIPES_DIR when it
// is set), the user's, and `projectFolder`'s (none when it is empty).
RecipeSources defaultSources(const QString &projectFolder = {});

// The recipes for `system` (Host::systemName()'s names): the empty
// one first, then by title. A folder whose recipe.json cannot be read is
// skipped, its reason added to `problems` when given, as are the
// departures from the schema of those read (Recipes::load's warnings).
QList<Recipe> available(const RecipeSources &sources,
                        const QString &system = Host::systemName(),
                        QStringList *problems = nullptr);
// Those of `recipes` attached to `family` (Recipe::language), in order.
QList<Recipe> forLanguage(const QList<Recipe> &recipes, const QString &family);
// The recipe `id`, whatever its systems; nullopt when no folder has it.
std::optional<Recipe> find(const RecipeSources &sources, const QString &id);
// The recipes of `project`'s enabled languages (ProjectConfig::recipes),
// found in its default sources. The ids no folder has are added to
// `missing` when given.
QList<Recipe> recipesOf(const ProjectConfig &project, QStringList *missing = nullptr);
// The requirements of `project`'s recipes, in their order; none for a
// recipe that is not installed.
QList<ToolRequirement> requirementsOf(const ProjectConfig &project);

} // namespace RecipeLibrary
