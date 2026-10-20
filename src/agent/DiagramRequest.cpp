// SPDX-License-Identifier: GPL-3.0-only

#include "DiagramRequest.h"

namespace DiagramRequest {

QString prompt(const QString &kindTitle, const QString &relativePath, const QString &description)
{
    return QStringLiteral(
               "Draw a diagram in the file %2, which does not exist yet, following the duet:diagrams skill. "
               "Kind of diagram: %1.\n"
               "What the user wants it to show:\n<request>\n%3\n</request>\n"
               "When the request is about this project, read the code it refers to first and draw what the code "
               "really does, in the user's words. Write that file only. Do not ask questions: when something is "
               "unclear, choose the most likely reading. Then answer in the language of the request, in one or "
               "two sentences, with what the diagram shows.")
        .arg(kindTitle, relativePath, description.trimmed());
}

} // namespace DiagramRequest
