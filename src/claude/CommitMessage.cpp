#include "CommitMessage.h"

#include <QRegularExpression>
#include <QStringList>

namespace CommitMessage {
namespace {

// What to read the changes with, for the request.
QString changesOf(Scope scope, const QString &pathspec)
{
    switch (scope) {
    case Scope::Staged:
        return QStringLiteral("the staged changes (git diff --cached)");
    case Scope::Everything: {
        const QString path = pathspec.isEmpty() ? QStringLiteral(".") : pathspec;
        return QStringLiteral("every change under %1, untracked files included: nothing is staged, so all of it "
                              "will be (git status and git diff HEAD -- %1)")
            .arg(path);
    }
    case Scope::Amend:
        return QStringLiteral("the last commit amended with the staged changes (git diff --cached HEAD^)");
    }
    return {};
}

// Wraps the body's long lines at 72 columns, as git expects; a list item's
// continuation lines are indented under its text. The summary line and
// indented lines (code) are kept as they are.
QString wrapBody(const QString &message)
{
    constexpr int width = 72;
    static const QRegularExpression item(QStringLiteral("^([-*]|\\d+[.)]) "));
    const QStringList lines = message.split(QLatin1Char('\n'));
    QStringList wrapped;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        if (i == 0 || line.size() <= width || line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t'))) {
            wrapped.append(line);
            continue;
        }
        const QRegularExpressionMatch match = item.match(line);
        const QString indent(match.hasMatch() ? match.capturedLength() : 0, QLatin1Char(' '));
        QString current;
        for (const QString &word : line.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (!current.isEmpty() && current.size() + 1 + word.size() > width) {
                wrapped.append(current);
                current = indent;
            } else if (!current.isEmpty() && current != indent) {
                current += QLatin1Char(' ');
            }
            current += word;
        }
        wrapped.append(current);
    }
    return wrapped.join(QLatin1Char('\n'));
}

} // namespace

QString request(Scope scope, const QString &pathspec)
{
    return QStringLiteral(
               "Write the commit message for %1, and put it in Duet's commit field with the set_commit_message "
               "tool; do not commit. Use what you know of why these changes were made. First line: a summary in "
               "the imperative mood, at most 72 characters, without a final period, in the style and the language "
               "of the recent commits (git log -n 15 --format=%s; ignore placeholder subjects such as '++' or "
               "'wip'). Then, only if the change is not obvious from the summary, a blank line and a short body "
               "that explains why, not how. No trailers such as Co-Authored-By.")
        .arg(changesOf(scope, pathspec));
}

QString tidy(const QString &message)
{
    QString tidied = message.trimmed();
    static const QRegularExpression fence(QStringLiteral("^```[\\w-]*\\n(.*)\\n```$"),
                                          QRegularExpression::DotMatchesEverythingOption);
    if (const QRegularExpressionMatch match = fence.match(tidied); match.hasMatch())
        tidied = match.captured(1).trimmed();
    if (tidied.size() >= 2 && tidied.startsWith(QLatin1Char('"')) && tidied.endsWith(QLatin1Char('"')))
        tidied = tidied.mid(1, tidied.size() - 2).trimmed();
    return wrapBody(tidied);
}

} // namespace CommitMessage
