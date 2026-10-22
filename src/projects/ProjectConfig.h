// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// How a project uses one language: the runtime it is written for (a JDK,
// a Node, a Python, a C++ standard) and the language server that completes
// it, both chosen when the project is imported.
struct LanguageSetup {
    QString language; // a family of ProjectLanguages: "java", "javascript", "cpp", "python"
    bool enabled = true;
    // "17" (JDK), "24" (Node), "3.12" (Python), "c++20" (C++ standard).
    QString runtimeVersion;
    // A runtime of the machine the user picked (JDK home, Python
    // executable); empty: any of `runtimeVersion`, Duet's own first.
    QString runtimePath;
    QString server; // a ToolCatalog server ("clangd"); empty: none
    // A version Duet downloads, or "system" for the one in the PATH.
    QString serverVersion;
    // The id of the recipe attached to the language (see RecipeLibrary),
    // one of this family's; empty: none.
    QString recipe;

    QVariantMap toVariant() const;
    static LanguageSetup fromVariant(const QVariantMap &map);
    bool operator==(const LanguageSetup &) const = default;
};

// A project Duet has opened: a folder and the setup of its languages.
struct ProjectConfig {
    QString path; // absolute
    QString name; // the folder's name
    QDateTime lastOpened;
    // False until its languages were chosen (by the user, or detected when
    // it was opened without the welcome window).
    bool configured = false;
    QList<LanguageSetup> languages;
    // Shell command run in an agent's worktree before its commits are
    // merged (see AgentMergeQueue); empty: none.
    QString agentCheckCommand;
    // True from its creation with a recipe that has setup commands until
    // they succeed (see Recipe::setupCommands).
    bool needsRecipeSetup = false;

    // The ids of the recipes attached to its enabled languages, in their
    // order.
    QStringList recipes() const;
    // The setup of `family`, if the project has one; valid until `languages`
    // changes.
    const LanguageSetup *language(const QString &family) const;
    LanguageSetup *language(const QString &family);

    QVariantMap toVariant() const;
    static ProjectConfig fromVariant(const QString &path, const QVariantMap &map);
};

// The languages a project is set up for, each grouping LanguageRegistry
// identifiers served by the same runtime and server.
namespace ProjectLanguages {

constexpr const char *systemVersion = "system";

// "java", "javascript", "cpp", "python", in the order the project setup
// shows them.
QStringList families();
// The family of a LanguageRegistry identifier ("tsx" → "javascript");
// empty for the others.
QString familyOf(const QString &languageId);
// "C/C++", "Java"...
QString title(const QString &family);

} // namespace ProjectLanguages
