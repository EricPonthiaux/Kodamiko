// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "McpRegistry.h"
#include "refactor/RefactoringOutcome.h"

#include <QList>
#include <QString>

#include <functional>
#include <utility>

class CodeIntelligence;
class CodeNavigator;
class EditorTabs;
class GitCommands;
class LanguageServers;
class McpRegistry;
class ProblemStore;
class QMenuBar;
class SymbolIndex;
class TerminalWidget;
struct ProcessSpec;

// The editor's MCP tools, resources and prompts, by domain. Each function
// declares its domain's in the registry; they act through the editor's
// objects given in the context, which outlive the registry.

struct KDKToolContext {
    // The project's folder (it changes when another one is opened).
    std::function<QString()> folder;
    EditorTabs *tabs = nullptr;
    CodeNavigator *navigator = nullptr;
    SymbolIndex *symbols = nullptr;
    LanguageServers *languageServers = nullptr;
    // The builds' problems too (null: the servers' only).
    ProblemStore *problems = nullptr;
    CodeIntelligence *intelligence = nullptr;
    GitCommands *git = nullptr;
    QMenuBar *menus = nullptr;
    // A message in the status bar.
    std::function<void(const QString &)> showMessage;
    // The terminal tabs (title, terminal), the Claude session's first.
    std::function<QList<std::pair<QString, TerminalWidget *>>()> terminals;
    // Opens a terminal tab running `spec`, shown to the user.
    std::function<TerminalWidget *(const QString &title, const ProcessSpec &spec)> addTerminal;
    // Shows the Diff mode on a file (relative path; empty: the list).
    std::function<void(const QString &)> showChanges;
    // Puts a commit message in the Diff mode's commit field, for the user to
    // review and commit.
    std::function<void(const QString &)> setCommitMessage;
    // The repository changed (files restored, staged, committed).
    std::function<void()> repositoryChanged;

    // A path given by Claude (relative to the project, or absolute), made
    // absolute.
    QString absolute(const QString &path) const;
    // A path as Claude is told it: relative to the project when inside it.
    QString relative(const QString &path) const;
};

namespace KDKTools {

// Server instructions: what Kodamiko is and when to use its tools.
QString instructions();

// A tool's Run from a named function of the context: each tool's work is a
// function of its own rather than a lambda inside the registration.
using ToolHandler = void (*)(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply);
using ImmediateToolHandler = McpResult (*)(const KDKToolContext &context, const QJsonObject &arguments);
McpTool::Run handledBy(const KDKToolContext &context, ToolHandler handler);
McpTool::Run handledBy(const KDKToolContext &context, ImmediateToolHandler handler);

// Files and tabs: editor state, open files, file text, opening, saving.
void registerEditorTools(McpRegistry &registry, const KDKToolContext &context);
// The project: search, files, symbols, editor commands, setup.
void registerWorkspaceTools(McpRegistry &registry, const KDKToolContext &context);
// The language servers: problems, definitions, usages, documentation,
// rename, quick fixes, formatting.
void registerCodeTools(McpRegistry &registry, const KDKToolContext &context);
// The refactorings: usages by kind, Rename File, Change Signature.
void registerRefactoringTools(McpRegistry &registry, const KDKToolContext &context);
// A refactoring's outcome as a tool's answer.
McpResult refactoringResult(const KDKToolContext &context, const RefactoringOutcome &outcome);
// Terminals: reading them, running a command.
void registerTerminalTools(McpRegistry &registry, const KDKToolContext &context);
// Git status, history, index, commits, snapshots; then the branch and
// integration tools.
void registerGitTools(McpRegistry &registry, const KDKToolContext &context);
// Branches, remotes, stashes.
void registerBranchTools(McpRegistry &registry, const KDKToolContext &context);
// Merge, rebase, cherry-pick, revert, and the operation in progress.
void registerIntegrationTools(McpRegistry &registry, const KDKToolContext &context);
// Diagrams: their content and selection, changes in the open diagram.
void registerDiagramTools(McpRegistry &registry, const KDKToolContext &context);

} // namespace KDKTools
