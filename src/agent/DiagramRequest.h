// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

// A diagram the agent draws on request, outside the editor's session: the
// prompt of an AgentFileRun that writes the diagram's file.
namespace DiagramRequest {

// What the agent is asked: draw a `kindTitle` in `relativePath`, showing
// `description`.
QString prompt(const QString &kindTitle, const QString &relativePath, const QString &description);

} // namespace DiagramRequest
