// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

// A diagram the agent draws on request, outside the editor's session: the
// prompt of an AgentFileRun that writes the diagram's file.
namespace DiagramRequest {

// What the agent is asked: draw a `kindTitle` in `relativePath`, showing
// `description`.
QString prompt(const QString &kindTitle, const QString &relativePath, const QString &description);

// The description, for prompt(), of a flowchart that explains how `code`
// works: `code` says where it is ("`AgentRun` in src/agent/AgentRun.cpp,
// line 21", see CodeRequest::describe). Its steps link to the code they stand
// for; its texts are in `language` ("French").
QString explanation(const QString &code, const QString &language);

// The name of the file of the flowchart that explains `symbol`, or without
// one the file `filePath`: "agent-run-flow.mmd" for AgentRun, with the
// diagram's `extension` ("mmd").
QString explanationFileName(const QString &symbol, const QString &filePath, const QString &extension);

} // namespace DiagramRequest
