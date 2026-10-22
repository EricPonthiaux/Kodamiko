// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "run/RunConfiguration.h"
#include "toolchains/InstallPlan.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

// What a recipe adds to the environment of the project's terminals, run
// configurations and agent sessions (see RecipeEnvironment::resolve).
struct RecipeEnvironment {
    // Windows: the MSVC developer environment, as an x64 Native Tools
    // prompt has it (cl, link, the SDK, Visual Studio's CMake and Ninja).
    bool visualStudio = false;
    QStringList pathFolders;          // put first in the PATH
    QMap<QString, QString> variables; // replace the editor's

    bool operator==(const RecipeEnvironment &) const = default;
};

// A tool a recipe requires, and where its files go in the environment of
// the project's terminals, runs and agent sessions. "{{home}}" stands for
// the tool's folder: the JDK's, Node's, the Qt kit's.
struct RecipeRequirement {
    ToolRequirement tool;
    // Put first in the PATH. A folder that does not exist is left out: a
    // recipe lists the layouts of every system ("{{home}}/bin" for Linux's
    // bin/node, "{{home}}" for Windows' node.exe).
    QStringList pathFolders;
    QMap<QString, QString> variables; // {"JAVA_HOME": "{{home}}"}

    bool operator==(const RecipeRequirement &) const = default;
};

// A prompt of the recipe, typed in the agent's prompt from the Project menu
// for the user to complete.
struct RecipePrompt {
    QString title;
    QString text;

    bool operator==(const RecipePrompt &) const = default;
};

// How to make one kind of project on one system (a Qt Widgets application
// with MSVC on Windows, a Python package...): its first files, its
// commands, its environment, and what the agent is told and can do there.
// A recipe is a folder:
//   recipe.json      the fields below (see Recipes::parse), described by
//                    tools/schemas/recipe.schema.json
//   files/           the new project's files; {{identifier}} in their paths
//                    and texts stands for ProjectCreation::identifierOf
//   instructions.md  added to the agent's system prompt (optional)
//   skills/          the agent's skills there: the recipe's folder is then
//                    loaded as a Claude Code plugin, named by its
//                    .claude-plugin/plugin.json (optional)
struct Recipe {
    QString id;     // the folder's name
    QString folder; // absolute
    QString title;  // "Qt Widgets · CMake · MSVC"
    QString description;
    // "windows", "linux", "macos"; empty: every system.
    QStringList systems;
    // The ProjectLanguages family it is attached to in a project ("cpp");
    // empty for a recipe of no language (the empty folder).
    QString language;
    RecipeEnvironment environment;
    // The tools its projects need on this system (a Node for npm, a JDK and
    // Maven, a Qt kit): Duet downloads those missing to ~/.duet/tools, and
    // puts them in the project's environment as each one says.
    QList<RecipeRequirement> requirements;
    // Written to the new project's .duet/run.json; added to an existing
    // project's own configurations.
    QList<RunConfiguration> runConfigurations;
    // Run once, one after the other, when the new project is first opened
    // (npm install, a CMake configure): what Build needs before it works.
    QStringList setupCommands;
    QList<RecipePrompt> prompts;
    QString instructions;

    // The folder of the recipe's skills for the agent (--plugin-dir), empty
    // when it has none.
    QString pluginFolder() const;
    // Whether it is made for `system` (Recipes::currentSystem()'s names).
    bool isFor(const QString &system) const;
    // Its run configurations with {{identifier}} replaced.
    QList<RunConfiguration> runConfigurationsFor(const QString &identifier) const;
    // The tools of its requirements, for InstallPlan.
    QList<ToolRequirement> tools() const;
    // Its setup commands as one configuration, run in the project's root
    // with {{identifier}} replaced; nullopt when it has none.
    std::optional<RunConfiguration> setupConfigurationFor(const QString &identifier) const;
};

namespace Recipes {

// The placeholder of the project's identifier in a recipe's files and
// commands.
constexpr auto identifierPlaceholder = "{{identifier}}";
// The placeholder of a required tool's folder in its "path" and "variables".
constexpr auto homePlaceholder = "{{home}}";
// The placeholders of a download's version and address, in its "url" and
// "checksumUrl".
constexpr auto versionPlaceholder = "{{version}}";
constexpr auto urlPlaceholder = "{{url}}";
// The name of the setup configuration, its terminal's title.
constexpr auto setupName = "Recipe Setup";

// "windows", "linux" or "macos".
QString currentSystem();

// The recipe of `recipeJson`, found in `folder`:
//   {"title": "...", "description": "...", "systems": ["windows"], "language": "cpp",
//    "environment": {"visualStudio": true,
//                    "path": ["C:/tools"], "variables": {"NAME": "value"}},
//    "requires": [{"tool": "jdk", "version": "21", "path": ["{{home}}/bin"],
//                  "variables": {"JAVA_HOME": "{{home}}"}},
//                 {"tool": "maven", "version": "3.9.16", "program": "mvn",
//                  "download": {"any": {"url": "https://.../apache-maven-{{version}}-bin.zip",
//                                       "checksumUrl": "{{url}}.sha512", "algorithm": "sha512",
//                                       "check": "bin/mvn"}},
//                  "path": ["{{home}}/bin"]},
//                 {"tool": "make"},
//                 {"tool": "qt", "version": "6.10", "kit": "gcc_64", "modules": ["qtcharts"],
//                  "systems": ["linux"], "path": ["{{home}}/bin"],
//                  "variables": {"CMAKE_PREFIX_PATH": "{{home}}"}}],
//    "run": [run.json configurations], "setup": ["npm install"],
//    "prompts": [{"title", "text"}]}
// A download is chosen for this system and processor: "windows-x64",
// "linux-aarch64"..., else "windows", "linux", "macos", else "any". It
// gives a "checksum" (the hash itself) or a "checksumUrl"; "algorithm" is
// "sha256" (the default) or "sha512"; "format" ("zip", "tar.gz", "7z") is
// read from the URL when it is not given. A download without a checksum
// or a known format is kept with its error, which the install plan shows.
// Nullopt, with `error` saying why, when it is not a JSON object with a
// title.
std::optional<Recipe> parse(const QByteArray &recipeJson, const QString &folder, QString *error);
// The recipe in `folder` (its recipe.json and instructions.md). What a
// recipe.json it reads departs from the schema is added to `warnings` when
// given, as "<file>:<line>: <message>" (see RecipeSchema::check).
std::optional<Recipe> load(const QString &folder, QString *error, QStringList *warnings = nullptr);

} // namespace Recipes
