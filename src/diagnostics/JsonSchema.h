// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Diagnostic.h"
#include "JsonTree.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

// Checks a JSON document against a JSON Schema (draft-07): the keywords
// SchemaStore's schemas use — type, enum, const, properties,
// patternProperties, additionalProperties, propertyNames, required, items,
// min/maxItems, uniqueItems, min/maxLength, pattern, minimum, maximum and
// their exclusive forms, anyOf, oneOf, allOf, not, and $ref within the
// schema or to the documents it is given. Others (format, if/then/else...)
// are ignored.
//
// Every problem is a warning, the schema may lag behind the program it
// describes. A key a closed object does not know, or one close to a known
// name ("modle" for "model"), is reported on the key.
class JsonSchema {
public:
    // `documents`: the other schemas its $refs name, by the file name the
    // $ref gives ("run.schema.json" for "run.schema.json#/definitions/x").
    // A $ref to a document it is not given is not followed.
    explicit JsonSchema(QJsonObject schema, const QHash<QString, QJsonObject> &documents = {});

    Diagnostics validate(const JsonNode &root) const;
    // The schema's description of the value at `path` (keys, and "0"...
    // for array items); empty if it has none.
    QString description(const QStringList &path) const;

private:
    struct Result;

    Result check(const JsonNode &node, const QJsonValue &schema, int depth) const;
    // False, with the problem added to `result`, when the node is not of a
    // type, const or enum value the schema allows.
    static bool checkKind(const JsonNode &node, const QJsonObject &schema, Result &result);
    void checkContent(const JsonNode &node, const QJsonObject &schema, int depth, Result &result) const;
    void checkString(const JsonNode &node, const QJsonObject &schema, Result &result) const;
    static void checkNumber(const JsonNode &node, const QJsonObject &schema, Result &result);
    void checkArray(const JsonNode &node, const QJsonObject &schema, int depth, Result &result) const;
    void checkAnyOf(const JsonNode &node, const QJsonArray &branches, int depth, Result &result) const;
    void checkObject(const JsonNode &node, const QJsonObject &schema, int depth, Result &result) const;
    // Checks a member against properties and patternProperties; false if
    // neither names it.
    bool checkKnownMember(const JsonMember &member, const QJsonObject &schema, int depth, Result &result) const;
    void checkUnknownMember(const JsonMember &member, const QJsonObject &schema, int depth, Result &result) const;
    // The schema of the child `key`, looked for in `schema` and its branches.
    QJsonValue childSchema(const QJsonValue &schema, const QString &key, int depth) const;
    QJsonObject resolve(const QJsonValue &schema) const;
    // The schema `ref` points at; undefined when it names an unknown document.
    QJsonValue target(const QString &ref) const;
    const QRegularExpression &regex(const QString &pattern) const;

    QJsonObject root_;
    QHash<QString, QJsonObject> documents_;
    mutable QHash<QString, QRegularExpression> regexes_;
};
