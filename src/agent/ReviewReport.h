// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "diagnostics/Problem.h"

#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

// The answer of a review run (AgentReviewRun), as JSON that follows
// schema(): a summary and the problems found, each with its place, its
// severity and a scenario where the code fails.
namespace ReviewReport {

struct Finding {
    QString path; // absolute
    int line = 1; // from 1
    Diagnostic::Severity severity = Diagnostic::Severity::Error;
    QString problem;
    QString scenario; // the input or state, and the wrong result

    bool operator==(const Finding &) const = default;
};

struct Report {
    QString summary;
    QList<Finding> findings; // the most severe first
};

// The JSON Schema the agent's answer must follow.
QJsonObject schema();

// The report in `answer`, its paths resolved against `projectFolder`;
// nullopt when `answer` is not a JSON object with a list of findings.
std::optional<Report> parse(const QString &answer, const QString &projectFolder);

// `finding` as a problem of the ProblemStore, told by `source`.
Problem toProblem(const Finding &finding, const QString &source);

// `findings` in Markdown, their paths relative to `projectFolder`, for the
// prompt of the session that wrote the code.
QString toMarkdown(const QList<Finding> &findings, const QString &projectFolder);

} // namespace ReviewReport
