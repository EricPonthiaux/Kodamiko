// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Recipe.h"

#include <QString>

// A new project made from a recipe: its files, then its .duet/run.json.
namespace ProjectCreation {

// "my_app" from "My App!": what the build files name the project (CMake
// target, Python package, npm package, Maven artifact). Lower case, letters,
// digits and underscores, never starting with a digit; "project" when
// nothing is left.
QString identifierOf(const QString &name);

// Whether create() accepts `folder`: it does not exist, or is an empty folder.
bool isFreeFolder(const QString &folder);

// Creates `folder` with the files of `recipe`, {{identifier}} replaced by
// identifierOf(the folder's name), and a .duet/run.json with its run
// configurations unless its files have one; `folder` must be free
// (isFreeFolder). Returns false with a message in `error` if a file cannot
// be written; the files written before the failure stay.
[[nodiscard]] bool create(const Recipe &recipe, const QString &folder, QString *error);

} // namespace ProjectCreation
