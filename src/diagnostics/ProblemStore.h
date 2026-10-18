#pragma once

#include "Problem.h"

#include <QHash>
#include <QObject>
#include <QStringList>

class LanguageServers;

// Every problem of the project: those the language servers report, and
// those read in the output of the builds (one set per run configuration,
// replaced by its next run). A build's problem on a line where a server
// reports one of the same severity is the same problem told twice: only
// the server's, which follows the text as it is typed, is kept.
class ProblemStore : public QObject {
    Q_OBJECT

public:
    // `servers` may be null: build problems only.
    explicit ProblemStore(LanguageServers *servers, QObject *parent = nullptr);

    // The problems `source`'s last run found, in place of the previous ones.
    void setBuildProblems(const QString &source, const Problems &problems);
    void clearBuildProblems();

    // The servers' problems of `path` (errors and warnings), then the builds'.
    Problems problems(const QString &path) const;
    Problems serverProblems(const QString &path) const;
    Problems buildProblems(const QString &path) const;
    // The files with problems, sorted.
    QStringList files() const;
    QStringList filesWithBuildProblems() const;

signals:
    void changed(const QString &path);

private:
    LanguageServers *servers_;
    QHash<QString, QHash<QString, Problems>> build_; // by source, then by path
};
