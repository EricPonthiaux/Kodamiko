// SPDX-License-Identifier: GPL-3.0-only

#include "GitCommandTranslations.h"

#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <utility>

namespace GitCommandTranslations {

namespace {

using ToolCalls = QList<ToolCall>;
using Translation = ToolCalls (*)(const QStringList &arguments);

// A git command's arguments, sorted out.
struct GitArguments {
    QStringList options;                // "-a", "--amend"…, clustered ones apart
    QHash<QString, QStringList> values; // by option: "-m" → its messages
    QStringList operands;
    QStringList paths; // after "--"

    bool hasAny(std::initializer_list<const char *> names) const
    {
        return std::ranges::any_of(names, [this](const char *name) { return options.contains(QLatin1String(name)); });
    }

    QStringList valuesOf(std::initializer_list<const char *> names) const
    {
        QStringList result;
        for (const char *name : names)
            result += values.value(QLatin1String(name));
        return result;
    }
};

// Reads the option at `at` into `parsed`, with its value when it takes one;
// returns the index of the last word read.
qsizetype readOption(GitArguments &parsed,
                     const QStringList &arguments,
                     qsizetype at,
                     const QSet<QString> &valueOptions)
{
    const QString &word = arguments.at(at);
    const qsizetype equals = word.indexOf(QLatin1Char('='));
    if (word.startsWith(QLatin1String("--")) && equals > 0) {
        parsed.options.append(word.left(equals));
        parsed.values[word.left(equals)].append(word.mid(equals + 1));
        return at;
    }
    if (valueOptions.contains(word)) {
        parsed.options.append(word);
        parsed.values[word].append(arguments.value(at + 1));
        return at + 1;
    }
    if (word.startsWith(QLatin1String("--"))) {
        parsed.options.append(word);
        return at;
    }
    // Clustered short options ("-am" is "-a -m"): an option taking a value
    // takes the rest of the word, or the next word.
    for (qsizetype c = 1; c < word.size(); ++c) {
        const QString option = QLatin1Char('-') + word.at(c);
        parsed.options.append(option);
        if (!valueOptions.contains(option))
            continue;
        const QString rest = word.mid(c + 1);
        parsed.values[option].append(rest.isEmpty() ? arguments.value(at + 1) : rest);
        return rest.isEmpty() ? at + 1 : at;
    }
    return at;
}

GitArguments parsed(const QStringList &arguments, const QSet<QString> &valueOptions = {})
{
    GitArguments result;
    for (qsizetype at = 0; at < arguments.size(); ++at) {
        const QString &word = arguments.at(at);
        if (word == QLatin1String("--")) {
            result.paths = arguments.mid(at + 1);
            break;
        }
        if (word.startsWith(QLatin1Char('-')) && word != QLatin1String("-"))
            at = readOption(result, arguments, at, valueOptions);
        else
            result.operands.append(word);
    }
    return result;
}

// A message written with a heredoc, as Claude does:
// "$(cat <<'EOF'\nSubject\n\nBody\nEOF\n)" is "Subject\n\nBody".
QString messageText(const QString &written)
{
    static const QRegularExpression heredoc(
        QStringLiteral("^\\$\\(\\s*cat\\s+<<-?\\s*['\"]?(\\w+)['\"]?\\s*\\n(.*)\\n\\s*\\1\\s*\\)\\s*$"),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = heredoc.match(written);
    return match.hasMatch() ? match.captured(2) : written;
}

ToolCall call(const char *tool, QJsonObject arguments = {}, QString note = {})
{
    return {.tool = QLatin1String(tool), .arguments = std::move(arguments), .note = std::move(note)};
}

QJsonArray list(const QStringList &items)
{
    return QJsonArray::fromStringList(items);
}

// "stash@{2}" or "2" → 2, nothing → 0 (the most recent); nullopt otherwise.
std::optional<int> stashIndex(const QString &written)
{
    static const QRegularExpression reference(QStringLiteral("^(?:stash@\\{(\\d+)\\}|(\\d+))$"));
    if (written.isEmpty())
        return 0;
    const QRegularExpressionMatch match = reference.match(written);
    if (!match.hasMatch())
        return std::nullopt;
    return (match.captured(1) + match.captured(2)).toInt();
}

// --- Translations, one per git command; empty when the command has no
// tool doing the same (or reads only).

ToolCalls translateAdd(const QStringList &arguments)
{
    const GitArguments a = parsed(arguments);
    if (a.hasAny({"-p", "--patch", "-i", "--interactive", "-e", "--edit", "-N", "--intent-to-add", "-n", "--dry-run"}))
        return {};
    QStringList paths = a.operands + a.paths;
    if (paths.isEmpty() && a.hasAny({"-A", "--all", "-u", "--update"}))
        paths = {QStringLiteral(".")};
    if (paths.isEmpty())
        return {};
    return {call("stage_files", {{QStringLiteral("paths"), list(paths)}})};
}

ToolCalls translateCommit(const QStringList &arguments)
{
    static const QSet<QString> valueOptions{QStringLiteral("-m"), QStringLiteral("--message"), QStringLiteral("-F"),
                                            QStringLiteral("-C"), QStringLiteral("-c"),        QStringLiteral("-t")};
    const GitArguments a = parsed(arguments, valueOptions);
    const QStringList messages = a.valuesOf({"-m", "--message"});
    const bool hasOtherSource = a.hasAny({"-F", "--file", "-C", "-c", "--reuse-message", "--reedit-message", "-t",
                                          "--template", "--fixup", "--squash", "-p", "--patch", "--interactive"});
    if (messages.isEmpty() || hasOtherSource || !a.operands.isEmpty() || !a.paths.isEmpty())
        return {};
    QStringList paragraphs;
    for (const QString &message : messages)
        paragraphs.append(messageText(message));
    QJsonObject commitArguments{{QStringLiteral("message"), paragraphs.join(QStringLiteral("\n\n"))}};
    if (a.hasAny({"--amend"}))
        commitArguments.insert(QStringLiteral("amend"), true);
    ToolCalls calls;
    if (a.hasAny({"-a", "--all"}))
        calls.append(call("stage_files", {{QStringLiteral("paths"), QJsonArray{}}},
                          QStringLiteral("the modified files get_git_status lists as unstaged")));
    calls.append(call("commit", commitArguments));
    return calls;
}

ToolCalls translateRestore(const QStringList &arguments)
{
    const GitArguments a = parsed(arguments, {QStringLiteral("-s"), QStringLiteral("--source")});
    const QStringList paths = a.operands + a.paths;
    if (paths.isEmpty() || a.hasAny({"-s", "--source", "-p", "--patch", "--ours", "--theirs"}))
        return {};
    const bool staged = a.hasAny({"-S", "--staged"});
    if (staged && !a.hasAny({"-W", "--worktree"}))
        return {call("stage_files", {{QStringLiteral("paths"), list(paths)}, {QStringLiteral("unstage"), true}})};
    QJsonObject discardArguments{{QStringLiteral("paths"), list(paths)}};
    if (staged)
        discardArguments.insert(QStringLiteral("staged"), true);
    return {call("discard_changes", discardArguments)};
}

// `git checkout [HEAD] -- paths`.
ToolCalls translateCheckoutOfFiles(const QStringList &source, const QStringList &paths)
{
    if (source.isEmpty())
        return {call("discard_changes", {{QStringLiteral("paths"), list(paths)}})};
    if (source == QStringList{QStringLiteral("HEAD")})
        return {call("discard_changes", {{QStringLiteral("paths"), list(paths)}, {QStringLiteral("staged"), true}})};
    return {};
}

ToolCalls translateCheckout(const QStringList &arguments)
{
    const GitArguments a = parsed(arguments, {QStringLiteral("-b"), QStringLiteral("-B")});
    if (a.hasAny({"-p", "--patch", "--detach", "--orphan", "-m", "--merge", "--conflict"}))
        return {};
    const QStringList created = a.valuesOf({"-b", "-B"});
    if (!created.isEmpty()) {
        if (!a.operands.isEmpty()) // a start point: switch_branch has none
            return {};
        return {call("switch_branch", {{QStringLiteral("name"), created.first()}, {QStringLiteral("create"), true}})};
    }
    if (a.hasAny({"--ours", "--theirs"})) {
        const QString side = a.hasAny({"--ours"}) ? QStringLiteral("current") : QStringLiteral("incoming");
        return {call("resolve_conflicts",
                     {{QStringLiteral("paths"), list(a.operands + a.paths)}, {QStringLiteral("side"), side}})};
    }
    if (!a.paths.isEmpty())
        return translateCheckoutOfFiles(a.operands, a.paths);
    if (a.operands == QStringList{QStringLiteral(".")})
        return {call("discard_changes", {{QStringLiteral("paths"), list(a.operands)}})};
    if (a.operands.size() == 1 && a.operands.first() != QLatin1String("-"))
        return {call("switch_branch", {{QStringLiteral("name"), a.operands.first()}},
                     QStringLiteral("or discard_changes if it names a file"))};
    return {};
}

ToolCalls translateSwitch(const QStringList &arguments)
{
    const GitArguments a = parsed(arguments, {QStringLiteral("-c"), QStringLiteral("-C"), QStringLiteral("--create"),
                                              QStringLiteral("--force-create")});
    if (a.hasAny({"-d", "--detach", "--orphan"}))
        return {};
    const QStringList created = a.valuesOf({"-c", "-C", "--create", "--force-create"});
    if (!created.isEmpty() && a.operands.isEmpty())
        return {call("switch_branch", {{QStringLiteral("name"), created.first()}, {QStringLiteral("create"), true}})};
    const bool namesABranch = created.isEmpty() && a.operands.size() == 1 && a.operands.first() != QLatin1String("-");
    if (namesABranch)
        return {call("switch_branch", {{QStringLiteral("name"), a.operands.first()}})};
    return {};
}

ToolCalls translateBranch(const QStringList &arguments)
{
    const GitArguments a = parsed(arguments);
    if (a.hasAny({"-r", "--remotes", "-a", "--all"}))
        return {};
    if (a.hasAny({"-d", "-D", "--delete"})) {
        const bool force = a.hasAny({"-D", "-f", "--force"});
        ToolCalls calls;
        for (const QString &name : a.operands) {
            QJsonObject deleteArguments{{QStringLiteral("name"), name}};
            if (force)
                deleteArguments.insert(QStringLiteral("force"), true);
            calls.append(call("delete_branch", deleteArguments));
        }
        return calls;
    }
    if (!a.hasAny({"-m", "-M", "--move"}))
        return {}; // listing, creating, tracking: no change worth a tool
    if (a.operands.size() == 1)
        return {call("rename_branch",
                     {{QStringLiteral("name"), QStringLiteral("<current branch>")},
                      {QStringLiteral("newName"), a.operands.first()}},
                     QStringLiteral("the current branch's name is in get_git_status"))};
    if (a.operands.size() == 2)
        return {call("rename_branch",
                     {{QStringLiteral("name"), a.operands.first()}, {QStringLiteral("newName"), a.operands.last()}})};
    return {};
}

// --abort, --continue and --skip of a merge, rebase, cherry-pick or revert.
std::optional<ToolCalls> translateOperationStep(const GitArguments &a)
{
    if (a.hasAny({"--abort"}))
        return ToolCalls{call("abort_operation")};
    if (a.hasAny({"--continue"}))
        return ToolCalls{call("continue_operation")};
    if (a.hasAny({"--skip"}))
        return ToolCalls{call("skip_commit")};
    return std::nullopt;
}

// Whether every option is one of `allowed`: the tool does the same.
bool hasOnlyOptions(const GitArguments &a, const QSet<QString> &allowed)
{
    return std::ranges::all_of(a.options, [&allowed](const QString &option) { return allowed.contains(option); });
}

ToolCalls translateMerge(const QStringList &arguments)
{
    const GitArguments a = parsed(arguments, {QStringLiteral("-m"), QStringLiteral("-s"), QStringLiteral("-X")});
    if (const std::optional<ToolCalls> step = translateOperationStep(a))
        return *step;
    const bool isPlain =
        hasOnlyOptions(a, {QStringLiteral("--no-edit"), QStringLiteral("-q"), QStringLiteral("--quiet")});
    if (!isPlain || a.operands.size() != 1)
        return {};
    return {call("merge_branch", {{QStringLiteral("branch"), a.operands.first()}})};
}

ToolCalls translateRebase(const QStringList &arguments)
{
    const GitArguments a = parsed(arguments);
    if (const std::optional<ToolCalls> step = translateOperationStep(a))
        return *step;
    const bool isPlain = hasOnlyOptions(a, {QStringLiteral("-q"), QStringLiteral("--quiet")});
    if (!isPlain || a.operands.size() != 1)
        return {};
    return {call("merge_branch", {{QStringLiteral("branch"), a.operands.first()}, {QStringLiteral("rebase"), true}})};
}

// cherry-pick and revert: one call per commit, in order.
ToolCalls translateCommits(const QStringList &arguments, const char *tool, const QSet<QString> &allowed)
{
    const GitArguments a = parsed(arguments);
    if (const std::optional<ToolCalls> step = translateOperationStep(a))
        return *step;
    const bool namesRanges = std::ranges::any_of(a.operands, [](const QString &id) { return id.contains(".."); });
    if (!hasOnlyOptions(a, allowed) || a.operands.isEmpty() || namesRanges)
        return {};
    ToolCalls calls;
    for (const QString &id : a.operands)
        calls.append(call(tool, {{QStringLiteral("id"), id}}));
    return calls;
}

ToolCalls translateCherryPick(const QStringList &arguments)
{
    return translateCommits(arguments, "cherry_pick", {});
}

ToolCalls translateRevert(const QStringList &arguments)
{
    return translateCommits(arguments, "revert_commit", {QStringLiteral("--no-edit")});
}

// How `git stash` was written: its message is an option or the operands.
enum class StashForm {
    Push, // git stash [push] [-m message]
    Save, // git stash save [message]
};

// git stash [push|save] [-m message] [-u]
ToolCalls translateStashPush(const QStringList &arguments, StashForm form)
{
    const bool isSave = form == StashForm::Save;
    const GitArguments a = parsed(arguments, {QStringLiteral("-m"), QStringLiteral("--message")});
    const bool isPartial = a.hasAny({"-p", "--patch", "-k", "--keep-index", "-S", "--staged", "--pathspec-from-file"});
    if (isPartial || !a.paths.isEmpty() || (!isSave && !a.operands.isEmpty()))
        return {};
    QJsonObject stashArguments;
    const QString message = isSave ? a.operands.join(QLatin1Char(' ')) : a.valuesOf({"-m", "--message"}).join(' ');
    if (!message.isEmpty())
        stashArguments.insert(QStringLiteral("message"), message);
    if (!a.hasAny({"-u", "--include-untracked", "-a", "--all"}))
        stashArguments.insert(QStringLiteral("keepNewFiles"), true);
    return {call("stash_changes", stashArguments)};
}

ToolCalls translateStash(const QStringList &arguments)
{
    const QString action = arguments.value(0);
    if (action.isEmpty() || action.startsWith(QLatin1Char('-')))
        return translateStashPush(arguments, StashForm::Push);
    const QStringList rest = arguments.mid(1);
    if (action == QLatin1String("push") || action == QLatin1String("save"))
        return translateStashPush(rest, action == QLatin1String("save") ? StashForm::Save : StashForm::Push);
    const GitArguments a = parsed(rest);
    const std::optional<int> index = stashIndex(a.operands.value(0));
    if (!index || !a.options.isEmpty() || a.operands.size() > 1)
        return {};
    if (action == QLatin1String("apply") || action == QLatin1String("pop")) {
        QJsonObject applyArguments{{QStringLiteral("index"), *index}};
        if (action == QLatin1String("apply"))
            applyArguments.insert(QStringLiteral("keep"), true);
        return {call("apply_stash", applyArguments)};
    }
    if (action == QLatin1String("drop"))
        return {call("drop_stash", {{QStringLiteral("index"), *index}})};
    return {};
}

ToolCalls translateFetch(const QStringList &)
{
    return {call("fetch", {}, QStringLiteral("it fetches every remote"))};
}

ToolCalls translatePull(const QStringList &arguments)
{
    if (!parsed(arguments).operands.isEmpty()) // another branch than the upstream
        return {};
    return {call("pull", {}, QStringLiteral("it follows the user's pull.rebase and pull.ff settings"))};
}

ToolCalls translatePush(const QStringList &arguments)
{
    const GitArguments a = parsed(arguments, {QStringLiteral("-o"), QStringLiteral("--push-option")});
    const bool isPlain = hasOnlyOptions(
        a, {QStringLiteral("-u"), QStringLiteral("--set-upstream"), QStringLiteral("-q"), QStringLiteral("--quiet")});
    // "git push", "git push origin", "git push -u origin HEAD": the current
    // branch to its remote, as the tool does.
    const bool sendsTheCurrentBranch =
        a.operands.size() < 2 || (a.operands.size() == 2 && a.operands.last() == QLatin1String("HEAD"));
    if (!isPlain || !sendsTheCurrentBranch)
        return {};
    return {call("push")};
}

ToolCalls translateReset(const QStringList &arguments)
{
    const GitArguments a = parsed(arguments);
    if (!a.options.isEmpty()) // --hard, --soft, --patch…: no tool
        return {};
    QStringList operands = a.operands;
    const bool startsAtHead = operands.value(0) == QLatin1String("HEAD");
    if (startsAtHead)
        operands.removeFirst();
    const bool namesFiles = startsAtHead || !a.paths.isEmpty();
    if (operands.isEmpty() && a.paths.isEmpty())
        return {call("stage_files",
                     {{QStringLiteral("paths"), list({QStringLiteral(".")})}, {QStringLiteral("unstage"), true}})};
    if (!namesFiles) // "git reset HEAD~1" moves the branch: no tool
        return {};
    return {
        call("stage_files", {{QStringLiteral("paths"), list(operands + a.paths)}, {QStringLiteral("unstage"), true}})};
}

ToolCalls translateMove(const QStringList &arguments)
{
    const GitArguments a = parsed(arguments);
    const QStringList operands = a.operands + a.paths;
    if (!a.options.isEmpty() || operands.size() != 2)
        return {};
    return {call("rename_file",
                 {{QStringLiteral("path"), operands.first()}, {QStringLiteral("newPath"), operands.last()}},
                 QStringLiteral("it also updates what refers to the file"))};
}

const QHash<QString, Translation> &translations()
{
    static const QHash<QString, Translation> byCommand{
        {QStringLiteral("add"), &translateAdd},
        {QStringLiteral("commit"), &translateCommit},
        {QStringLiteral("restore"), &translateRestore},
        {QStringLiteral("checkout"), &translateCheckout},
        {QStringLiteral("switch"), &translateSwitch},
        {QStringLiteral("branch"), &translateBranch},
        {QStringLiteral("merge"), &translateMerge},
        {QStringLiteral("rebase"), &translateRebase},
        {QStringLiteral("cherry-pick"), &translateCherryPick},
        {QStringLiteral("revert"), &translateRevert},
        {QStringLiteral("stash"), &translateStash},
        {QStringLiteral("fetch"), &translateFetch},
        {QStringLiteral("pull"), &translatePull},
        {QStringLiteral("push"), &translatePush},
        {QStringLiteral("reset"), &translateReset},
        {QStringLiteral("mv"), &translateMove},
    };
    return byCommand;
}

} // namespace

QList<ToolCall> translate(const QString &subcommand, const QStringList &arguments)
{
    const Translation translation = translations().value(subcommand);
    return translation ? translation(arguments) : QList<ToolCall>();
}

} // namespace GitCommandTranslations
