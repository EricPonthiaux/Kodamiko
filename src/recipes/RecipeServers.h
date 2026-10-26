// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Recipe.h"
#include "lsp/LanguageServers.h"

#include <QList>
#include <QString>

class Toolchains;

// The language servers and server options a project's recipes bring.
namespace RecipeServers {

// What `recipes` add to the servers of the project in `projectFolder`:
// their servers whose tool `toolchains` has installed (a server whose tool
// is missing is left out: the install plan offers to download it), and
// their options for the servers Duet knows, merged in the recipes' order.
// "{{home}}" stands for the server's tool folder, "{{project}}" for
// `projectFolder`.
LanguageServers::Additions additionsOf(const QList<Recipe> &recipes,
                                       const QString &projectFolder,
                                       const Toolchains &toolchains);

} // namespace RecipeServers
