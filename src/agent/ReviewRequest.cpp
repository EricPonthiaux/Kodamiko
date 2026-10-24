// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewRequest.h"

#include <QDir>

namespace ReviewRequest {

namespace {

// Lines of a failing check's output given to the reviewer.
constexpr int reportedOutputLines = 40;

QString patchNote(const QString &patchPath)
{
    if (patchPath.isEmpty())
        return {};
    return QStringLiteral("The changes to review are in the unified diff %1: read it, then read the files it "
                          "touches for their context. Judge what the changes do; report a problem of unchanged code "
                          "only when the changes cause it. Give the lines of the files as they are now on disk.\n\n")
        .arg(QDir::toNativeSeparators(patchPath));
}

QString checkNote(const std::optional<Check> &check)
{
    if (!check)
        return {};
    if (check->result.hasPassed())
        return QStringLiteral("Before this review, Duet ran the project's check `%1`: it passed. Do not report what "
                              "it would catch (code that does not build, failing tests, lint warnings).\n\n")
            .arg(check->command);
    return QStringLiteral("Before this review, Duet ran the project's check `%1`: it failed, with exit code %2. The "
                          "last lines of its output:\n```\n%3\n```\nReport first the problems of the code that "
                          "cause this failure.\n\n")
        .arg(check->command, QString::number(check->result.exitCode), check->result.lastLines(reportedOutputLines));
}

} // namespace

QString prompt(const Request &request)
{
    const QString intro =
        QStringLiteral("Review %1. You did not write this code and do not know why it was written: judge it as it "
                       "is. Read what you need of the project to understand it, and hold it to the project's rules "
                       "(CLAUDE.md) if it has any.\n\n")
            .arg(request.subject);
    const QString findings = QStringLiteral(
        "Report only the problems that make the code wrong: a bug, a crash, an error lost or ignored, a broken "
        "invariant, a rule of the project broken. No style preferences, no hypothetical improvements. For each "
        "problem give its file and line, its severity, what is wrong, and a concrete scenario where it fails (the "
        "input or state, and the wrong result); the most severe first. If you find none, leave the list empty: do "
        "not invent problems to have some.\n\n"
        "Change nothing. Answer with the structured output: a short summary of the code as a whole, and the "
        "findings.");
    return intro + patchNote(request.patchPath) + checkNote(request.check) + findings;
}

QString followUp(const QString &agentName, const QString &subject, const QString &review)
{
    return QStringLiteral("A separate %1 session, which did not see this conversation, reviewed %2 and reports "
                          "the problems below. Check each one against the code: tell me which are real and how you "
                          "would fix them, and which you think are wrong and why. Apply nothing until I "
                          "agree.\n\n%3")
        .arg(agentName, subject, review);
}

} // namespace ReviewRequest
