#pragma once

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

struct DuetToolContext {
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
    // The repository changed (files restored, staged, committed).
    std::function<void()> repositoryChanged;

    // A path given by Claude (relative to the project, or absolute), made
    // absolute.
    QString absolute(const QString &path) const;
    // A path as Claude is told it: relative to the project when inside it.
    QString relative(const QString &path) const;
};

namespace DuetTools {

// Server instructions: what Duet is and when to use its tools.
QString instructions();

// Files and tabs: editor state, open files, file text, opening, saving.
void registerEditorTools(McpRegistry &registry, const DuetToolContext &context);
// The project: search, files, symbols, editor commands, setup.
void registerWorkspaceTools(McpRegistry &registry, const DuetToolContext &context);
// The language servers: problems, definitions, usages, documentation,
// rename, quick fixes, formatting.
void registerCodeTools(McpRegistry &registry, const DuetToolContext &context);
// Terminals: reading them, running a command.
void registerTerminalTools(McpRegistry &registry, const DuetToolContext &context);
// Git status, history, commits, snapshots; then the branch tools.
void registerGitTools(McpRegistry &registry, const DuetToolContext &context);
// Branches, remotes, stashes, merge and rebase.
void registerBranchTools(McpRegistry &registry, const DuetToolContext &context);
// Diagrams: their content and selection, changes in the open diagram.
void registerDiagramTools(McpRegistry &registry, const DuetToolContext &context);

} // namespace DuetTools
