// SPDX-License-Identifier: GPL-3.0-only

#include "DiagramRequest.h"

#include <QFileInfo>
#include <QRegularExpression>

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

QString explanation(const QString &code, const QString &language)
{
    return QStringLiteral(
               "Explain how this code works, as a flowchart: %1.\n"
               "Read that code, and what it calls in this project as far as it helps to understand it. For a class, "
               "show how its methods work together in its main use: what calls what, in which order, and the "
               "decisions on the way. For a function or a few lines, show their steps, decisions and loops as the "
               "code runs them, the error paths included. Name each step in plain words rather than with code, and "
               "keep to about 25 elements. Link each element that stands for a function, a method or a class of "
               "this project to it with a `click` line, as the skill's \"Links to the code\" says: the user "
               "Ctrl+clicks it to read that code. Name the file that defines it, where its body is: in C or C++ the "
               "source file, not the header that only declares it; in TypeScript the implementation, not a "
               "signature or a .d.ts. Qualify a member with its class: Class::member in C and C++, Class.member in "
               "Java, JavaScript and TypeScript. The element where the flow starts links to the code this request "
               "is about. Write the diagram's texts and your answer in %2.")
        .arg(code, language);
}

QString explanationFileName(const QString &symbol, const QString &filePath, const QString &extension)
{
    static const QRegularExpression wordStart(QStringLiteral("([a-z0-9])([A-Z])"));
    static const QRegularExpression separators(QStringLiteral("[^a-z0-9]+"));
    QString name = symbol.isEmpty() ? QFileInfo(filePath).completeBaseName() : symbol;
    name = name.replace(wordStart, QStringLiteral("\\1-\\2")).toLower().replace(separators, QStringLiteral("-"));
    name.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (name.isEmpty())
        name = QStringLiteral("code");
    return QStringLiteral("%1-flow.%2").arg(name, extension);
}

} // namespace DiagramRequest
