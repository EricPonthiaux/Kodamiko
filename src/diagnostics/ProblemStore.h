// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Problem.h"

#include <QHash>
#include <QObject>
#include <QStringList>

class LanguageServers;

// Every problem of the project: those the language servers report, those
// read in the output of the builds (one set per run configuration,
// replaced by its next run), and the findings of the reviews (one set per
// review, see ReviewSession). A build's problem on a line where a server
// reports one of the same severity is the same problem told twice: only
// the server's, which follows the text as it is typed, is kept. A review's
// findings are always kept: they are not what a compiler says.
class ProblemStore : public QObject {
    Q_OBJECT

public:
    // `servers` may be null: build problems only.
    explicit ProblemStore(LanguageServers *servers, QObject *parent = nullptr);

    // The problems `source`'s last run found, in place of the previous ones.
    void setBuildProblems(const QString &source, const Problems &problems);
    void clearBuildProblems();
    // The findings of `review` (its own identifier), in place of its
    // previous ones; none removes the review.
    void setReviewProblems(const QString &review, const Problems &problems);

    // The servers' problems of `path` (errors and warnings), then the
    // builds' that no server reports, then the reviews'.
    Problems problems(const QString &path) const;
    Problems serverProblems(const QString &path) const;
    Problems buildProblems(const QString &path) const;
    // The builds' problems of `path` on a line where no server reports one
    // of the same severity.
    Problems unreportedBuildProblems(const QString &path) const;
    Problems reviewProblems(const QString &path) const;
    // The files with problems, sorted.
    QStringList files() const;
    QStringList filesWithBuildProblems() const;

signals:
    void changed(const QString &path);

private:
    using ProblemSets = QHash<QString, QHash<QString, Problems>>; // by set, then by path

    // `problems` in place of `sets`' set `key`, `source` written in each.
    void replaceSet(ProblemSets &sets, const QString &key, const QString &source, const Problems &problems);

    LanguageServers *servers_;
    ProblemSets build_;   // by run configuration
    ProblemSets reviews_; // by review
};
