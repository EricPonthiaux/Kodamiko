// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArrayView>
#include <QList>
#include <QString>

// Where a recipe.json departs from Kodamiko's recipe schema
// (tools/schemas/recipe.schema.json, its "run" from run.schema.json): what
// Recipes::parse silently skips or reads otherwise — an unknown key, a
// "kind" it does not know, a download without a checksum.
namespace RecipeSchema {

struct Problem {
    int line = 0; // from 1
    QString message;

    bool operator==(const Problem &) const = default;
};

// The problems of `recipeJson`, in the text's order; none when it follows
// the schema. A text that is not JSON is one problem, where it goes wrong.
QList<Problem> check(QByteArrayView recipeJson);

} // namespace RecipeSchema
