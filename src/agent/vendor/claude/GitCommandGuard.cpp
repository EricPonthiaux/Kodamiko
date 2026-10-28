// SPDX-License-Identifier: GPL-3.0-only

#include "GitCommandGuard.h"

#include "GitCommandTranslations.h"
#include "ShellCommandLine.h"

#include <QDir>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>

namespace GitCommandGuard {

namespace {

using GitCommandTranslations::ToolCall;

// One simple command of a shell line, its words unquoted.
using Words = QStringList;

// A git command found in a shell line.
struct GitCommand {
    std::optional<QString> directory; // where it runs; nullopt: unknown
    QString subcommand;               // "commit"
    QStringList arguments;
    QString text; // as written, for the reason
};

// Commands longer than this are cut in the reason (a commit message…).
constexpr qsizetype shownCommandLength = 80;

// --- Directories

// `target` from `current`; nullopt when it cannot be known without a shell
// ("~", "$HOME", "-") or `current` is unknown.
std::optional<QString> changedDirectory(const std::optional<QString> &current, const QString &target)
{
    const bool needsAShell = target.isEmpty() || target.startsWith(QLatin1Char('~')) ||
                             target.startsWith(QLatin1Char('$')) || target == QLatin1String("-");
    if (needsAShell)
        return std::nullopt;
    if (QDir::isAbsolutePath(target))
        return QDir::cleanPath(target);
    if (!current)
        return std::nullopt;
    return QDir::cleanPath(QDir(*current).filePath(target));
}

bool isInside(const QString &folder, const std::optional<QString> &directory)
{
    if (!directory)
        return false;
    const QString relative = QDir(folder).relativeFilePath(*directory);
    const bool isAbove = relative == QLatin1String("..") || relative.startsWith(QLatin1String("../"));
    return !isAbove && !QDir::isAbsolutePath(relative);
}

bool isChangeOfDirectory(const Words &words)
{
    return words.first() == QLatin1String("cd") || words.first() == QLatin1String("pushd");
}

// --- Git commands

bool isGitProgram(const QString &word)
{
    return word == QLatin1String("git") || word == QLatin1String("git.exe") || word.endsWith(QLatin1String("/git"));
}

// VARIABLE=value before a command.
bool isAssignment(const QString &word)
{
    static const QRegularExpression assignment(QStringLiteral("^[A-Za-z_]\\w*="));
    return assignment.match(word).hasMatch();
}

// The git command of `words`, run from `directory`; nullopt for another
// program.
std::optional<GitCommand> gitCommand(const Words &words, const std::optional<QString> &directory)
{
    qsizetype at = 0;
    while (at < words.size() && isAssignment(words.at(at)))
        ++at;
    if (at >= words.size() || !isGitProgram(words.at(at)))
        return std::nullopt;
    GitCommand git{.directory = directory, .text = words.mid(at).join(QLatin1Char(' '))};
    // Git's own options, before the subcommand.
    for (++at; at < words.size() && words.at(at).startsWith(QLatin1Char('-')); ++at) {
        const QString &option = words.at(at);
        if (option == QLatin1String("-C"))
            git.directory = changedDirectory(git.directory, words.value(++at));
        else if (option == QLatin1String("-c"))
            ++at;
        else if (option.startsWith(QLatin1String("--git-dir")) || option.startsWith(QLatin1String("--work-tree")))
            git.directory = std::nullopt;
    }
    if (at >= words.size())
        return std::nullopt;
    git.subcommand = words.at(at);
    git.arguments = words.mid(at + 1);
    return git;
}

// --- The reason

QString described(const ToolCall &toolCall)
{
    QString text =
        QStringLiteral("mcp__kodamiko__%1 %2")
            .arg(toolCall.tool, QString::fromUtf8(QJsonDocument(toolCall.arguments).toJson(QJsonDocument::Compact)));
    if (!toolCall.note.isEmpty())
        text += QStringLiteral(" (%1)").arg(toolCall.note);
    return text;
}

QString shown(const QString &command)
{
    const QString firstLine = command.section(QLatin1Char('\n'), 0, 0);
    if (firstLine.size() <= shownCommandLength && firstLine.size() == command.size())
        return command;
    return firstLine.left(shownCommandLength) + QStringLiteral("…");
}

// "- `git add a` → mcp__kodamiko__stage_files {…}", or nothing.
std::optional<QString> redirection(const GitCommand &git)
{
    const QList<ToolCall> calls = GitCommandTranslations::translate(git.subcommand, git.arguments);
    if (calls.isEmpty())
        return std::nullopt;
    QStringList steps;
    for (const ToolCall &toolCall : calls)
        steps.append(described(toolCall));
    return QStringLiteral("- `%1` → %2").arg(shown(git.text), steps.join(QStringLiteral(", then ")));
}

QString reasonFor(const QStringList &redirections)
{
    return QStringLiteral(
               "In this project, change the git repository through Kodamiko's tools, not git in a shell: each tool "
               "asks "
               "the user with a plain description of what it does, takes a Kodamiko snapshot before what is hard to "
               "undo (restore_snapshot brings it back), runs one git operation at a time with Kodamiko's own git work, "
               "asks for remote credentials in the editor instead of leaving the shell waiting, and refreshes "
               "Kodamiko's Diff, History and branch views at once. Nothing of this command line ran. Instead:\n%1\n"
               "Run the rest of the command line, if any, on its own. Reading git in a shell (status, log, diff, "
               "show) is fine; get_git_status, get_file_history and get_commit read the same as JSON.")
        .arg(redirections.join(QLatin1Char('\n')));
}

} // namespace

std::optional<QString> refusal(const QString &commandLine, const Location &location)
{
    std::optional<QString> directory =
        location.workingDirectory.isEmpty() ? location.projectFolder : location.workingDirectory;
    QStringList redirections;
    for (const Words &words : ShellCommandLine::commands(commandLine)) {
        if (isChangeOfDirectory(words)) {
            directory = changedDirectory(directory, words.value(1));
            continue;
        }
        const std::optional<GitCommand> git = gitCommand(words, directory);
        if (!git || !isInside(location.projectFolder, git->directory))
            continue;
        if (const std::optional<QString> line = redirection(*git))
            redirections.append(*line);
    }
    if (redirections.isEmpty())
        return std::nullopt;
    return reasonFor(redirections);
}

} // namespace GitCommandGuard
