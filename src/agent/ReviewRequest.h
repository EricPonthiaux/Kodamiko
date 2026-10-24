// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ProjectCheck.h"

#include <QString>

#include <optional>

// What a review run (AgentReviewRun) is asked, and how its findings go
// back to the session that wrote the code. The reviewer gets the code and
// what Duet knows for sure about it (the project's check), never the
// reasoning that wrote it.
namespace ReviewRequest {

// The project's check, run before the review.
struct Check {
    QString command;
    ProjectCheck::Result result;
};

struct Request {
    QString subject; // what is reviewed, in words: "src/a.cpp, lines 2-3"
    // A unified diff of the changes to review, to read; empty to review
    // the code as it is.
    QString patchPath;
    std::optional<Check> check; // nullopt when the project has none
};

// The prompt of the review run: only the problems that make the code
// wrong, each with its line and a scenario where it fails, nothing
// changed, the answer as ReviewReport::schema() says.
QString prompt(const Request &request);

// The request typed in the writing session's prompt that hands it `review`,
// the findings of a review of `subject` by `agentName`: check each one,
// apply nothing until the user agrees.
QString followUp(const QString &agentName, const QString &subject, const QString &review);

} // namespace ReviewRequest
