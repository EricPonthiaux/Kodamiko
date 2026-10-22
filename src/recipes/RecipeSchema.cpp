// SPDX-License-Identifier: GPL-3.0-only

#include "RecipeSchema.h"

#include "diagnostics/JsonSchema.h"
#include "diagnostics/JsonTree.h"

#include <QFile>
#include <QJsonDocument>

#include <algorithm>

namespace {

QJsonObject readSchema(const QString &fileName)
{
    QFile file(QStringLiteral(":/schemas/") + fileName);
    if (!file.open(QIODevice::ReadOnly))
        return {}; // checks nothing
    return QJsonDocument::fromJson(file.readAll()).object();
}

const JsonSchema &recipeSchema()
{
    static const QString runSchema = QStringLiteral("run.schema.json");
    static const JsonSchema schema(readSchema(QStringLiteral("recipe.schema.json")),
                                   {{runSchema, readSchema(runSchema)}});
    return schema;
}

RecipeSchema::Problem problemAt(QByteArrayView text, int offset, const QString &message)
{
    const QByteArrayView before = text.first(std::clamp(offset, 0, int(text.size())));
    return {.line = int(std::ranges::count(before, '\n')) + 1, .message = message};
}

} // namespace

namespace RecipeSchema {

QList<Problem> check(QByteArrayView recipeJson)
{
    const JsonParseResult parsed = parseJson(recipeJson);
    if (!parsed.root)
        return {problemAt(recipeJson, parsed.errorOffset, parsed.error)};
    QList<Problem> problems;
    for (const Diagnostic &diagnostic : recipeSchema().validate(*parsed.root))
        problems.append(problemAt(recipeJson, diagnostic.start, diagnostic.message));
    return problems;
}

} // namespace RecipeSchema
