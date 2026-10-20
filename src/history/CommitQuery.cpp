// SPDX-License-Identifier: GPL-3.0-only

#include "CommitQuery.h"

#include <QObject>
#include <QRegularExpression>

#include <algorithm>

namespace {

// The day `value` names: an ISO date or a time ago; nullopt if it is neither.
std::optional<QDate> dateOf(const QString &value, QDate today)
{
    if (const QDate date = QDate::fromString(value, Qt::ISODate); date.isValid())
        return date;
    // "7d", "2w", "3m", "1y": that long ago.
    static const QRegularExpression relativeDate(QStringLiteral(R"(^(\d+)([dwmy])$)"));
    const QRegularExpressionMatch match = relativeDate.match(value);
    if (!match.hasMatch())
        return std::nullopt;
    const int count = match.captured(1).toInt();
    const QChar unit = match.captured(2).front();
    if (unit == u'd')
        return today.addDays(-count);
    if (unit == u'w')
        return today.addDays(-7 * qint64(count));
    if (unit == u'm')
        return today.addMonths(-count);
    return today.addYears(-count);
}

void setDate(CommitQuery &query, std::optional<QDate> &bound, const QString &value, QDate today)
{
    bound = dateOf(value, today);
    if (!bound && query.error.isEmpty())
        query.error = QObject::tr("Unknown date \"%1\": write 2024-03-31, or 7d, 2w, 3m, 1y for a time ago").arg(value);
}

void setContents(CommitQuery &query, ContentSearch search)
{
    if (query.contents && query.error.isEmpty())
        query.error = QObject::tr("Only one contents: or diff: term is searched");
    if (query.contents)
        return;
    if (search.match == ContentMatch::ChangedLine) {
        const QRegularExpression expression(search.pattern);
        if (!expression.isValid()) {
            if (query.error.isEmpty())
                query.error =
                    QObject::tr("Invalid regular expression \"%1\": %2").arg(search.pattern, expression.errorString());
            return;
        }
    }
    query.contents = std::move(search);
}

// Adds the term `key:value`; false if `key` names no filter.
bool addTerm(CommitQuery &query, const QString &key, const QString &value, QDate today)
{
    if (key == u"author")
        query.authors.append(value);
    else if (key == u"message")
        query.messageTexts.append(value);
    else if (key == u"after")
        setDate(query, query.after, value, today);
    else if (key == u"before")
        setDate(query, query.before, value, today);
    else if (key == u"contents")
        setContents(query, {ContentMatch::Occurrences, value});
    else if (key == u"diff")
        setContents(query, {ContentMatch::ChangedLine, value});
    else
        return false;
    return true;
}

// A word of the query: in the message (which holds the summary), the
// author, the start of the ID, or a branch or tag name.
bool hasWord(const CommitSummary &commit, const QString &word)
{
    return commit.message.contains(word, Qt::CaseInsensitive) || commit.author.contains(word, Qt::CaseInsensitive) ||
           commit.email.contains(word, Qt::CaseInsensitive) || commit.id.startsWith(word, Qt::CaseInsensitive) ||
           commit.refs.contains(word, Qt::CaseInsensitive);
}

bool hasAuthor(const CommitSummary &commit, const QString &author)
{
    return commit.author.contains(author, Qt::CaseInsensitive) || commit.email.contains(author, Qt::CaseInsensitive);
}

} // namespace

CommitQuery CommitQuery::parse(const QString &text, QDate today)
{
    // An optional "key:", then a quoted value (its closing quote may be
    // missing while typing) or a value up to the next space.
    static const QRegularExpression termPattern(QStringLiteral(R"re((?:([a-z]+):)?(?:"([^"]*)"?|(\S*)))re"));
    CommitQuery query;
    for (const QRegularExpressionMatch &term : termPattern.globalMatch(text)) {
        const QString key = term.captured(1);
        const bool isQuoted = term.capturedStart(2) >= 0;
        const QString value = isQuoted ? term.captured(2) : term.captured(3);
        // Between terms, or "author:" whose value is still being typed.
        if (value.isEmpty())
            continue;
        if (key.isEmpty() || !addTerm(query, key, value, today))
            query.words.append(key.isEmpty() ? value : term.captured(0));
    }
    return query;
}

bool CommitQuery::matches(const CommitSummary &commit) const
{
    const QDate day = commit.date.date();
    const bool isAfter = !after || day >= *after;
    const bool isBefore = !before || day <= *before;
    const auto inMessage = [&commit](const QString &text) {
        return commit.message.contains(text, Qt::CaseInsensitive);
    };
    return isAfter && isBefore &&
           std::ranges::all_of(words, [&commit](const QString &word) { return hasWord(commit, word); }) &&
           std::ranges::all_of(authors, [&commit](const QString &author) { return hasAuthor(commit, author); }) &&
           std::ranges::all_of(messageTexts, inMessage);
}

bool CommitQuery::keepsEveryCommit() const
{
    return words.isEmpty() && authors.isEmpty() && messageTexts.isEmpty() && !after && !before;
}
