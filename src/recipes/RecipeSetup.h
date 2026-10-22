// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Recipe.h"
#include "terminal/ProcessSpec.h"

#include <QStringList>

class Toolchains;

// What a recipe sets up on this machine for the project that uses it.
namespace RecipeSetup {

// The variables and PATH folders `environment` stands for here: Visual
// Studio's developer environment, then the recipe's variables and folders.
// What cannot be found (Visual Studio) is left out and said in `problems`,
// for the user.
EnvironmentAdditions environmentAdditions(const RecipeEnvironment &environment, QStringList *problems);

// The folders and variables of `requirements`, "{{home}}" replaced by the
// folder of each tool found in `toolchains` (Duet's or the machine's). A
// tool that is missing, or that has no folder (a program of the PATH), adds
// nothing: the install plan offers to download it, or says to install it.
EnvironmentAdditions requirementAdditions(const QList<RecipeRequirement> &requirements, const Toolchains &toolchains);

} // namespace RecipeSetup
