// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/CommitLog.h"

#include <QDate>
#include <QString>
#include <QStringList>

#include <optional>

// A search of the history, as typed in its filter field: words, and terms
// that name what they search.
//
//   fix crash             every word in the summary, the message, the
//                         author, the ID (its start) or a branch or tag
//   author:ada            the author's name or email
//   message:"null check"  the whole message
//   after:2024-03-01      from that day on; also 7d, 2w, 3m, 1y ago
//   before:2024-06-30     up to that day, included
//   contents:parseUrl     changes that add or remove the text (git log -S)
//   diff:"TODO\(\w+\)"    changed lines matching the regular expression
//                         (git log -G)
//
// Words and terms are all required; quotes keep spaces in a value. Text is
// matched without case, but contents: and diff: with case, as git does.
struct CommitQuery {
    // Where a term is in the text typed.
    struct Span {
        qsizetype start = 0;
        qsizetype length = 0;

        bool operator==(const Span &) const = default;
    };

    QStringList words;
    QStringList authors;
    QStringList messageTexts;
    std::optional<QDate> after;
    std::optional<QDate> before;
    // Read with the log (CommitLog::read): the changes of every commit.
    std::optional<ContentSearch> contents;
    // The first term that could not be read, said for the user; the query
    // goes without it. Empty if every term was read.
    QString error;
    // The terms read ("author:ada"), in the order typed; not the words, nor
    // a term that could not be read.
    QList<Span> termSpans;

    // Reads `text`; relative dates count back from `today`.
    static CommitQuery parse(const QString &text, QDate today = QDate::currentDate());

    // Whether `commit` passes every filter but the content search.
    bool matches(const CommitSummary &commit) const;
    // Whether it keeps every commit read: no filter but the content search.
    bool keepsEveryCommit() const;
};
