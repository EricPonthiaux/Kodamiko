// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "run/RunConfiguration.h"
#include "toolchains/InstallPlan.h"

#include <QByteArray>
#include <QJsonObject>
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

// How the recipe's projects run a server Kodamiko knows: its version, and
// the runtime it runs on, whatever the project's (jdtls 1.62.0 needs a JDK
// 25, for a Java 21 project too).
struct RecipeServer {
    QString version;         // "1.62.0"
    ToolRequirement runtime; // {"jdk", "25"}; no tool: none said

    bool operator==(const RecipeServer &) const = default;
};

// A prompt of the recipe, typed in the agent's prompt from the Project menu
// for the user to complete.
struct RecipePrompt {
    QString title;
    QString text;

    bool operator==(const RecipePrompt &) const = default;
};

// One value a recipe's parameter offers, and the placeholders it sets with
// it: Angular 22 comes with TypeScript 6.0, Angular 21 with 5.9.
struct RecipeParameterValue {
    QString value;                    // "22"
    QMap<QString, QString> variables; // {"typescript": "~6.0.0"}

    bool operator==(const RecipeParameterValue &) const = default;
};

// A choice the new project is made with ("angular": the Angular version),
// offered when it is created and kept in the project
// (ProjectConfig::recipeValues). {{name}} stands for the value chosen, in
// the recipe's files, requirements, commands and servers.
struct RecipeParameter {
    QString name;                       // "angular"
    QString title;                      // "Angular"
    QList<RecipeParameterValue> values; // the first one by default

    bool operator==(const RecipeParameter &) const = default;
};

// A language server the recipe brings for languages Kodamiko has none for
// (Angular's templates): a Node script of a tool the recipe requires.
struct RecipeLanguageServer {
    QString name; // "angular"
    QString tool; // the requirement that installs it
    // In the tool's folder, run with Node.
    QString script;
    // "{{home}}" stands for the tool's folder, "{{project}}" for the
    // project's.
    QStringList arguments;
    QStringList languages; // LanguageRegistry identifiers: "html"
    QString triggerCharacters;

    bool operator==(const RecipeLanguageServer &) const = default;
};

// What a recipe adds to the options of a server Kodamiko knows (the plugin
// of tsserver); "{{project}}" in their texts stands for the project's
// folder.
struct RecipeServerOptions {
    QJsonObject initialization;
    QJsonObject settings;

    bool operator==(const RecipeServerOptions &) const = default;
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
    // Maven, a Qt kit): Kodamiko downloads those missing to ~/.kodamiko/tools, and
    // puts them in the project's environment as each one says.
    QList<RecipeRequirement> requirements;
    // Written to the new project's .kodamiko/run.json; added to an existing
    // project's own configurations.
    QList<RunConfiguration> runConfigurations;
    // Run once, one after the other, when the new project is first opened
    // (npm install, a CMake configure): what Build needs before it works.
    QStringList setupCommands;
    QList<RecipePrompt> prompts;
    QString instructions;
    QList<RecipeParameter> parameters;
    // The placeholders withValues() replaced, by name ("angular": "22",
    // "typescript": "~6.0.0"): the new project's files get them too.
    QMap<QString, QString> values;
    QList<RecipeLanguageServer> languageServers;
    // By the name of a server Kodamiko knows ("typescript-language-server").
    QMap<QString, RecipeServerOptions> serverOptions;
    // By the name of a server Kodamiko knows ("jdtls"): its version and
    // runtime, in place of the project's choice and Kodamiko's catalog.
    QMap<QString, RecipeServer> servers;

    // The folder of the recipe's skills for the agent (--plugin-dir), empty
    // when it has none.
    QString pluginFolder() const;
    // Whether it is made for `system` (Host::systemName()'s names).
    bool isFor(const QString &system) const;
    // Its run configurations with {{identifier}} replaced.
    QList<RunConfiguration> runConfigurationsFor(const QString &identifier) const;
    // The tools of its requirements, then the runtimes of its servers, for
    // InstallPlan.
    QList<ToolRequirement> tools() const;
    // Its setup commands as one configuration, run in the project's root
    // with {{identifier}} replaced; nullopt when it has none.
    std::optional<RunConfiguration> setupConfigurationFor(const QString &identifier) const;
    // The recipe with each parameter's placeholders replaced, in its
    // requirements, commands, prompts, instructions and servers: by the
    // value `chosen` gives the parameter when it is one of its values, else
    // by its first value. Kept in `values` for the files.
    Recipe withValues(const QMap<QString, QString> &chosen) const;
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
// The placeholder of the project's folder in a server's arguments and
// options.
constexpr auto projectPlaceholder = "{{project}}";
// The name of the setup configuration, its terminal's title.
constexpr auto setupName = "Recipe Setup";

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
//    "prompts": [{"title", "text"}],
//    "parameters": [{"name": "angular", "title": "Angular",
//                    "values": [{"value": "22", "variables": {"typescript": "~6.0.0"}}, "21"]}],
//    "languageServers": [{"name": "angular", "tool": "angular-language-server",
//                         "script": "node_modules/@angular/language-server/index.js",
//                         "arguments": ["--stdio"], "languages": ["html"], "triggerCharacters": "<."}],
//    "serverOptions": {"typescript-language-server": {"initialization": {...}, "settings": {...}}},
//    "servers": {"jdtls": {"version": "1.62.0", "runtime": {"tool": "jdk", "version": "25"}}}}
// A download is chosen for this system and processor: "windows-x64",
// "linux-aarch64"..., else "windows", "linux", "macos", else "any". It
// gives a "checksum" (the hash itself) or a "checksumUrl"; "algorithm" is
// "sha256" (the default) or "sha512"; "format" ("zip", "tar.gz", "7z") is
// read from the URL when it is not given. A download of "npm" packages
// ({{version}} in their names) is installed with npm, checked by npm. A
// download without a checksum
// or a known format is kept with its error, which the install plan shows.
// Nullopt, with `error` saying why, when it is not a JSON object with a
// title.
std::optional<Recipe> parse(const QByteArray &recipeJson, const QString &folder, QString *error);
// The recipe in `folder` (its recipe.json and instructions.md). What a
// recipe.json it reads departs from the schema is added to `warnings` when
// given, as "<file>:<line>: <message>" (see RecipeSchema::check).
std::optional<Recipe> load(const QString &folder, QString *error, QStringList *warnings = nullptr);

} // namespace Recipes
