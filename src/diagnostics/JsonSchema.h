#pragma once

#include "Diagnostic.h"
#include "JsonTree.h"

#include <QHash>
#include <QJsonObject>
#include <QRegularExpression>

// Checks a JSON document against a JSON Schema (draft-07): the keywords
// SchemaStore's schemas use — type, enum, const, properties,
// patternProperties, additionalProperties, propertyNames, required, items,
// min/maxItems, uniqueItems, min/maxLength, pattern, minimum, maximum and
// their exclusive forms, anyOf, oneOf, allOf, not, and $ref within the
// schema. Others (format, if/then/else...) are ignored.
//
// Every problem is a warning, the schema may lag behind the program it
// describes. A key a closed object does not know, or one close to a known
// name ("modle" for "model"), is reported on the key.
class JsonSchema {
public:
    explicit JsonSchema(const QJsonObject &schema);

    Diagnostics validate(const JsonNode &root) const;
    // The schema's description of the value at `path` (keys, and "0"...
    // for array items); empty if it has none.
    QString description(const QStringList &path) const;

private:
    struct Result;

    Result check(const JsonNode &node, const QJsonValue &schema, int depth) const;
    void checkObject(const JsonNode &node, const QJsonObject &schema, int depth, Result &result) const;
    QJsonObject resolve(const QJsonValue &schema) const;
    const QRegularExpression &regex(const QString &pattern) const;

    QJsonObject root_;
    mutable QHash<QString, QRegularExpression> regexes_;
};
