// SPDX-License-Identifier: GPL-3.0-only

#include "ProblemStore.h"

#include "lsp/LanguageServers.h"
#include "lsp/LspProtocol.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

ProblemStore::ProblemStore(LanguageServers *servers, QObject *parent)
    : QObject(parent)
    , servers_(servers)
{
    if (servers_)
        connect(servers_, &LanguageServers::diagnosticsChanged, this, &ProblemStore::changed);
}

void ProblemStore::setBuildProblems(const QString &source, const Problems &problems)
{
    replaceSet(build_, source, source, problems);
}

void ProblemStore::setReviewProblems(const QString &review, const Problems &problems)
{
    // The findings keep the source their review gave them.
    replaceSet(reviews_, review, QString(), problems);
}

void ProblemStore::replaceSet(ProblemSets &sets, const QString &key, const QString &source, const Problems &problems)
{
    QSet<QString> touched;
    for (auto it = sets.value(key).cbegin(); it != sets.value(key).cend(); ++it)
        touched.insert(it.key());
    QHash<QString, Problems> byPath;
    for (const Problem &problem : problems) {
        byPath[problem.path].append(problem);
        if (!source.isEmpty())
            byPath[problem.path].last().source = source;
        touched.insert(problem.path);
    }
    if (byPath.isEmpty())
        sets.remove(key);
    else
        sets.insert(key, byPath);
    for (const QString &path : touched)
        emit changed(path);
}

void ProblemStore::clearBuildProblems()
{
    QSet<QString> touched;
    for (const QHash<QString, Problems> &byPath : std::as_const(build_)) {
        for (auto it = byPath.cbegin(); it != byPath.cend(); ++it)
            touched.insert(it.key());
    }
    build_.clear();
    for (const QString &path : touched)
        emit changed(path);
}

Problems ProblemStore::serverProblems(const QString &path) const
{
    Problems problems;
    if (!servers_)
        return problems;
    for (const QJsonValue &value : servers_->diagnostics(path)) {
        const QJsonObject diagnostic = value.toObject();
        const int severity = diagnostic.value(QStringLiteral("severity")).toInt(1);
        if (severity > 2)
            continue; // information and hints stay quiet
        const LspProtocol::Range range = LspProtocol::rangeFrom(diagnostic.value(QStringLiteral("range")));
        Problem problem;
        problem.path = path;
        problem.line = range.start.line;
        problem.column = range.start.character;
        problem.endLine = range.end.line;
        problem.endColumn = range.end.character;
        problem.severity = severity == 1 ? Diagnostic::Severity::Error : Diagnostic::Severity::Warning;
        problem.message = diagnostic.value(QStringLiteral("message")).toString();
        problem.source = diagnostic.value(QStringLiteral("source")).toString();
        problems.append(problem);
    }
    return problems;
}

Problems ProblemStore::buildProblems(const QString &path) const
{
    // By source, in a stable order.
    QStringList sources = build_.keys();
    sources.sort();
    Problems problems;
    for (const QString &source : std::as_const(sources))
        problems += build_.value(source).value(path);
    return problems;
}

Problems ProblemStore::unreportedBuildProblems(const QString &path) const
{
    QSet<QPair<int, int>> reported; // line, severity
    for (const Problem &problem : serverProblems(path))
        reported.insert({problem.line, static_cast<int>(problem.severity)});
    Problems problems;
    for (const Problem &problem : buildProblems(path)) {
        if (!reported.contains({problem.line, static_cast<int>(problem.severity)}))
            problems.append(problem);
    }
    return problems;
}

Problems ProblemStore::reviewProblems(const QString &path) const
{
    Problems problems;
    for (const QHash<QString, Problems> &byPath : reviews_)
        problems += byPath.value(path);
    std::ranges::sort(problems, {}, &Problem::line);
    return problems;
}

Problems ProblemStore::problems(const QString &path) const
{
    return serverProblems(path) + unreportedBuildProblems(path) + reviewProblems(path);
}

QStringList ProblemStore::filesWithBuildProblems() const
{
    QSet<QString> files;
    for (const QHash<QString, Problems> &byPath : build_) {
        for (auto it = byPath.cbegin(); it != byPath.cend(); ++it)
            files.insert(it.key());
    }
    QStringList sorted(files.cbegin(), files.cend());
    sorted.sort();
    return sorted;
}

QStringList ProblemStore::files() const
{
    QSet<QString> files;
    for (const QString &path : filesWithBuildProblems())
        files.insert(path);
    for (const QHash<QString, Problems> &byPath : reviews_) {
        for (auto it = byPath.cbegin(); it != byPath.cend(); ++it)
            files.insert(it.key());
    }
    if (servers_) {
        for (const QString &path : servers_->filesWithDiagnostics()) {
            if (!serverProblems(path).isEmpty())
                files.insert(path);
        }
    }
    QStringList sorted(files.cbegin(), files.cend());
    sorted.sort();
    return sorted;
}
