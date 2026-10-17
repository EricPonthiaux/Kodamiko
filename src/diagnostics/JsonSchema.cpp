#include "JsonSchema.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

constexpr int maxDepth = 200;

QString tr(const char *text)
{
    return QCoreApplication::translate("JsonSchema", text);
}

QString compact(const QJsonValue &value)
{
    // QJsonDocument writes only objects and arrays.
    const QByteArray json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    QString text = QString::fromUtf8(json.mid(1, json.size() - 2));
    if (text.size() > 60)
        text = text.left(59) + QChar(0x2026);
    return text;
}

QString typeName(const QString &type)
{
    if (type == QLatin1String("string")) return tr("a string");
    if (type == QLatin1String("number")) return tr("a number");
    if (type == QLatin1String("integer")) return tr("an integer");
    if (type == QLatin1String("boolean")) return tr("true or false");
    if (type == QLatin1String("object")) return tr("an object");
    if (type == QLatin1String("array")) return tr("an array");
    if (type == QLatin1String("null")) return QStringLiteral("null");
    return type;
}

QString actualName(const JsonNode &node)
{
    switch (node.type) {
    case QJsonValue::String: return tr("a string");
    case QJsonValue::Double: return tr("a number");
    case QJsonValue::Bool: return node.scalar.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Object: return tr("an object");
    case QJsonValue::Array: return tr("an array");
    default: return QStringLiteral("null");
    }
}

bool hasType(const JsonNode &node, const QString &type)
{
    switch (node.type) {
    case QJsonValue::String: return type == QLatin1String("string");
    case QJsonValue::Bool: return type == QLatin1String("boolean");
    case QJsonValue::Object: return type == QLatin1String("object");
    case QJsonValue::Array: return type == QLatin1String("array");
    case QJsonValue::Null: return type == QLatin1String("null");
    case QJsonValue::Double: {
        const double number = node.scalar.toDouble();
        return type == QLatin1String("number") || (type == QLatin1String("integer") && std::floor(number) == number);
    }
    default: return false;
    }
}

QStringList typesOf(const QJsonObject &schema)
{
    const QJsonValue type = schema.value(QLatin1String("type"));
    if (type.isString())
        return {type.toString()};
    QStringList types;
    for (const QJsonValue &item : type.toArray())
        types.append(item.toString());
    return types;
}

// A whole object or array is not underlined, only its first character.
std::pair<int, int> rangeOf(const JsonNode &node)
{
    if (node.type == QJsonValue::Object || node.type == QJsonValue::Array)
        return {node.start, node.start + 1};
    return {node.start, node.end};
}

int editDistance(const QString &a, const QString &b)
{
    QList<int> previous(b.size() + 1);
    QList<int> current(b.size() + 1);
    for (qsizetype j = 0; j <= b.size(); ++j)
        previous[j] = int(j);
    for (qsizetype i = 1; i <= a.size(); ++i) {
        current[0] = int(i);
        for (qsizetype j = 1; j <= b.size(); ++j) {
            const int substitution = previous[j - 1] + (a[i - 1].toLower() == b[j - 1].toLower() ? 0 : 1);
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, substitution});
        }
        std::swap(previous, current);
    }
    return previous[b.size()];
}

// The known name `key` was probably meant for, or an empty string.
QString closestName(const QString &key, const QStringList &names)
{
    if (key.size() < 4)
        return {};
    QString best;
    int bestDistance = 3;
    for (const QString &name : names) {
        const int distance = editDistance(key, name);
        if (distance < bestDistance) {
            best = name;
            bestDistance = distance;
        }
    }
    return best;
}

// Why `text` does not match `pattern`, readable even when the pattern is
// long: a close word of the pattern ("Wirte" for "Write"), or what the
// schema says the value is, with its examples.
QString patternMessage(const QString &text, const QString &pattern, const QJsonObject &schema)
{
    static const QRegularExpression word(QStringLiteral("^[A-Za-z_]+"));
    static const QRegularExpression words(QStringLiteral("[A-Za-z_]{3,}"));
    const QString leading = word.match(text).captured();
    QStringList candidates;
    for (auto it = words.globalMatch(pattern); it.hasNext();)
        candidates.append(it.next().captured());
    if (!leading.isEmpty() && !candidates.contains(leading)) {
        if (const QString close = closestName(leading, candidates); !close.isEmpty())
            return tr("“%1” does not have the expected form: did you mean “%2”?").arg(text, close);
    }
    if (pattern.size() <= 40)
        return tr("“%1” does not match the expected form %2").arg(text, pattern);
    QString message = tr("“%1” does not have the expected form").arg(text);
    const QString description = schema.value(QLatin1String("description")).toString().section(QLatin1Char('\n'), 0, 0);
    if (!description.isEmpty())
        message += QStringLiteral(" (%1)").arg(description.trimmed().chopped(description.trimmed().endsWith(QLatin1Char('.')) ? 1 : 0));
    QStringList examples;
    for (const QJsonValue &example : schema.value(QLatin1String("examples")).toArray()) {
        if (examples.size() < 3)
            examples.append(example.toString());
    }
    if (!examples.isEmpty())
        message += tr(", e.g. %1").arg(examples.join(QStringLiteral(", ")));
    return message;
}

} // namespace

struct JsonSchema::Result {
    Diagnostics diagnostics;
    // False when the value is not of the kind the schema wants at all
    // (type, const, enum): anyOf then reports what it expected instead of a
    // branch's details.
    bool typeMatched = true;
    // What the schema wanted instead, when typeMatched is false: "a
    // string", "\"auto\""...
    QStringList expected;

    void add(int start, int end, const QString &message)
    {
        diagnostics.append({Diagnostic::Severity::Warning, start, std::max(end, start + 1), message});
    }
    void add(const JsonNode &node, const QString &message)
    {
        const auto [start, end] = rangeOf(node);
        add(start, end, message);
    }
};

JsonSchema::JsonSchema(const QJsonObject &schema)
    : root_(schema)
{
}

Diagnostics JsonSchema::validate(const JsonNode &root) const
{
    Diagnostics diagnostics = check(root, root_, 0).diagnostics;

    // A key written twice: Claude reads only the last one.
    const std::function<void(const JsonNode &)> findDuplicates = [&](const JsonNode &node) {
        for (qsizetype i = 0; i < node.members.size(); ++i) {
            const JsonMember &member = node.members[i];
            for (qsizetype j = i + 1; j < node.members.size(); ++j) {
                if (node.members[j].key == member.key) {
                    diagnostics.append({Diagnostic::Severity::Warning, member.keyStart, member.keyEnd,
                                        tr("Duplicate key: only the last “%1” counts").arg(member.key)});
                    break;
                }
            }
            findDuplicates(member.value);
        }
        for (const JsonNode &item : node.items)
            findDuplicates(item);
    };
    findDuplicates(root);

    std::stable_sort(diagnostics.begin(), diagnostics.end(),
                     [](const Diagnostic &a, const Diagnostic &b) { return a.start < b.start; });
    return diagnostics;
}

QJsonObject JsonSchema::resolve(const QJsonValue &schema) const
{
    QJsonObject object = schema.toObject();
    for (int hops = 0; hops < 32 && object.contains(QLatin1String("$ref")); ++hops) {
        const QString ref = object.value(QLatin1String("$ref")).toString();
        if (!ref.startsWith(QLatin1String("#")))
            return {}; // another document: not followed
        QJsonValue target = root_;
        for (const QString &part : ref.mid(1).split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
            QString name = part;
            name.replace(QLatin1String("~1"), QLatin1String("/")).replace(QLatin1String("~0"), QLatin1String("~"));
            target = target.toObject().value(name);
        }
        object = target.toObject();
    }
    return object;
}

const QRegularExpression &JsonSchema::regex(const QString &pattern) const
{
    auto it = regexes_.find(pattern);
    if (it == regexes_.end())
        it = regexes_.insert(pattern, QRegularExpression(pattern));
    return *it;
}

JsonSchema::Result JsonSchema::check(const JsonNode &node, const QJsonValue &schemaValue, int depth) const
{
    Result result;
    if (depth > maxDepth)
        return result;
    if (schemaValue.isBool()) {
        if (!schemaValue.toBool()) {
            result.typeMatched = false;
            result.add(node, tr("Not allowed here"));
        }
        return result;
    }
    const QJsonObject schema = resolve(schemaValue);

    const QStringList types = typesOf(schema);
    if (!types.isEmpty() && std::none_of(types.begin(), types.end(), [&](const QString &type) { return hasType(node, type); })) {
        QStringList names;
        for (const QString &type : types)
            names.append(typeName(type));
        result.typeMatched = false;
        result.expected = names;
        result.add(node, tr("Expected %1, not %2").arg(names.join(tr(" or ")), actualName(node)));
        return result;
    }
    const QJsonValue value = node.toJson();
    if (schema.contains(QLatin1String("const")) && value != schema.value(QLatin1String("const"))) {
        result.typeMatched = false;
        result.expected = {compact(schema.value(QLatin1String("const")))};
        result.add(node, tr("Expected %1").arg(result.expected.first()));
        return result;
    }
    if (schema.contains(QLatin1String("enum"))) {
        const QJsonArray values = schema.value(QLatin1String("enum")).toArray();
        if (!values.contains(value)) {
            QStringList names;
            for (const QJsonValue &item : values)
                names.append(compact(item));
            result.typeMatched = false;
            result.expected = names;
            result.add(node, tr("Expected one of: %1").arg(names.join(QStringLiteral(", "))));
            return result;
        }
    }

    switch (node.type) {
    case QJsonValue::String: {
        const QString text = node.scalar.toString();
        const qsizetype length = text.toUcs4().size();
        if (schema.contains(QLatin1String("minLength")) && length < schema.value(QLatin1String("minLength")).toInt())
            result.add(node, length == 0 ? tr("Must not be empty")
                                         : tr("At least %1 characters").arg(schema.value(QLatin1String("minLength")).toInt()));
        if (schema.contains(QLatin1String("maxLength")) && length > schema.value(QLatin1String("maxLength")).toInt())
            result.add(node, tr("At most %1 characters").arg(schema.value(QLatin1String("maxLength")).toInt()));
        if (schema.contains(QLatin1String("pattern"))) {
            const QString pattern = schema.value(QLatin1String("pattern")).toString();
            const QRegularExpression &expression = regex(pattern);
            if (expression.isValid() && !expression.match(text).hasMatch())
                result.add(node, patternMessage(text, pattern, schema));
        }
        break;
    }
    case QJsonValue::Double: {
        const double number = node.scalar.toDouble();
        const auto bound = [&](const char *keyword, auto violates, const char *message) {
            const QJsonValue limit = schema.value(QLatin1String(keyword));
            if (limit.isDouble() && violates(number, limit.toDouble()))
                result.add(node, tr(message).arg(limit.toDouble()));
        };
        bound("minimum", std::less<double>(), QT_TRANSLATE_NOOP("JsonSchema", "At least %1"));
        bound("maximum", std::greater<double>(), QT_TRANSLATE_NOOP("JsonSchema", "At most %1"));
        bound("exclusiveMinimum", std::less_equal<double>(), QT_TRANSLATE_NOOP("JsonSchema", "Greater than %1"));
        bound("exclusiveMaximum", std::greater_equal<double>(), QT_TRANSLATE_NOOP("JsonSchema", "Less than %1"));
        break;
    }
    case QJsonValue::Array: {
        const QJsonValue items = schema.value(QLatin1String("items"));
        for (qsizetype i = 0; i < node.items.size(); ++i) {
            const QJsonValue itemSchema = items.isArray() ? items.toArray().at(i) : items;
            if (!itemSchema.isUndefined())
                result.diagnostics += check(node.items[i], itemSchema, depth + 1).diagnostics;
        }
        if (schema.contains(QLatin1String("minItems")) && node.items.size() < schema.value(QLatin1String("minItems")).toInt())
            result.add(node, QCoreApplication::translate("JsonSchema", "At least %n item(s)", nullptr, schema.value(QLatin1String("minItems")).toInt()));
        if (schema.contains(QLatin1String("maxItems")) && node.items.size() > schema.value(QLatin1String("maxItems")).toInt())
            result.add(node, QCoreApplication::translate("JsonSchema", "At most %n item(s)", nullptr, schema.value(QLatin1String("maxItems")).toInt()));
        if (schema.value(QLatin1String("uniqueItems")).toBool()) {
            for (qsizetype i = 1; i < node.items.size(); ++i) {
                const QJsonValue item = node.items[i].toJson();
                for (qsizetype j = 0; j < i; ++j) {
                    if (node.items[j].toJson() == item) {
                        result.add(node.items[i], tr("Already in the list"));
                        break;
                    }
                }
            }
        }
        break;
    }
    case QJsonValue::Object:
        checkObject(node, schema, depth, result);
        break;
    default:
        break;
    }

    for (const QJsonValue &part : schema.value(QLatin1String("allOf")).toArray())
        result.diagnostics += check(node, part, depth + 1).diagnostics;

    // oneOf is checked as anyOf: schemas often have branches that overlap.
    for (const char *keyword : {"anyOf", "oneOf"}) {
        const QJsonArray branches = schema.value(QLatin1String(keyword)).toArray();
        if (branches.isEmpty())
            continue;
        QList<Result> results;
        bool matched = false;
        for (const QJsonValue &branch : branches) {
            results.append(check(node, branch, depth + 1));
            if (results.last().diagnostics.isEmpty()) {
                matched = true;
                break;
            }
        }
        if (matched)
            continue;
        const Result *best = nullptr;
        for (const Result &branch : std::as_const(results)) {
            if (branch.typeMatched && (!best || branch.diagnostics.size() < best->diagnostics.size()))
                best = &branch;
        }
        if (best) {
            result.diagnostics += best->diagnostics;
            continue;
        }
        // Of no kind the branches accept: what each expected.
        QStringList expected;
        for (const Result &branch : std::as_const(results)) {
            for (const QString &item : branch.expected) {
                if (!expected.contains(item))
                    expected.append(item);
            }
        }
        result.typeMatched = false;
        result.expected = expected;
        result.add(node, tr("Expected %1, not %2").arg(expected.join(tr(" or ")), actualName(node)));
    }

    if (schema.contains(QLatin1String("not")) && check(node, schema.value(QLatin1String("not")), depth + 1).diagnostics.isEmpty())
        result.add(node, tr("Not allowed here"));
    return result;
}

void JsonSchema::checkObject(const JsonNode &node, const QJsonObject &schema, int depth, Result &result) const
{
    const QJsonObject properties = schema.value(QLatin1String("properties")).toObject();
    const QJsonObject patterns = schema.value(QLatin1String("patternProperties")).toObject();
    const QJsonValue additional = schema.value(QLatin1String("additionalProperties"));
    const QJsonValue propertyNames = schema.value(QLatin1String("propertyNames"));

    for (const JsonMember &member : node.members) {
        bool known = false;
        if (properties.contains(member.key)) {
            known = true;
            result.diagnostics += check(member.value, properties.value(member.key), depth + 1).diagnostics;
        }
        for (auto it = patterns.begin(); it != patterns.end(); ++it) {
            if (regex(it.key()).match(member.key).hasMatch()) {
                known = true;
                result.diagnostics += check(member.value, it.value(), depth + 1).diagnostics;
            }
        }
        if (!known) {
            const QString suggestion = closestName(member.key, properties.keys());
            if (additional.isBool() && !additional.toBool()) {
                result.add(member.keyStart, member.keyEnd,
                           suggestion.isEmpty() ? tr("Unknown key “%1”").arg(member.key)
                                                : tr("Unknown key “%1”: did you mean “%2”?").arg(member.key, suggestion));
            } else if (additional.isObject()) {
                result.diagnostics += check(member.value, additional, depth + 1).diagnostics;
            } else if (!suggestion.isEmpty()) {
                result.add(member.keyStart, member.keyEnd,
                           tr("Unknown key “%1”: did you mean “%2”?").arg(member.key, suggestion));
            }
        }
        if (!propertyNames.isUndefined()) {
            JsonNode key;
            key.type = QJsonValue::String;
            key.scalar = member.key;
            key.start = member.keyStart;
            key.end = member.keyEnd;
            result.diagnostics += check(key, propertyNames, depth + 1).diagnostics;
        }
    }

    for (const QJsonValue &required : schema.value(QLatin1String("required")).toArray()) {
        const QString key = required.toString();
        if (std::none_of(node.members.begin(), node.members.end(), [&](const JsonMember &m) { return m.key == key; }))
            result.add(node.start, node.start + 1, tr("Missing “%1”").arg(key));
    }
}

QString JsonSchema::description(const QStringList &path) const
{
    // The schema of a child, looked for in `schema` and its branches.
    const std::function<QJsonValue(const QJsonValue &, const QString &, int)> child =
        [&](const QJsonValue &schemaValue, const QString &key, int depth) -> QJsonValue {
        if (depth > 8)
            return QJsonValue(QJsonValue::Undefined);
        const QJsonObject schema = resolve(schemaValue);
        const QJsonObject properties = schema.value(QLatin1String("properties")).toObject();
        if (properties.contains(key))
            return properties.value(key);
        const QJsonObject patterns = schema.value(QLatin1String("patternProperties")).toObject();
        for (auto it = patterns.begin(); it != patterns.end(); ++it) {
            if (regex(it.key()).match(key).hasMatch())
                return it.value();
        }
        if (schema.value(QLatin1String("additionalProperties")).isObject())
            return schema.value(QLatin1String("additionalProperties"));
        bool isIndex = false;
        key.toInt(&isIndex);
        if (isIndex && schema.value(QLatin1String("items")).isObject())
            return schema.value(QLatin1String("items"));
        for (const char *keyword : {"anyOf", "oneOf", "allOf"}) {
            for (const QJsonValue &branch : schema.value(QLatin1String(keyword)).toArray()) {
                if (const QJsonValue found = child(branch, key, depth + 1); !found.isUndefined())
                    return found;
            }
        }
        return QJsonValue(QJsonValue::Undefined);
    };

    QJsonValue schema = root_;
    for (const QString &key : path) {
        schema = child(schema, key, 0);
        if (schema.isUndefined())
            return {};
    }
    const QString own = schema.toObject().value(QLatin1String("description")).toString();
    return own.isEmpty() ? resolve(schema).value(QLatin1String("description")).toString() : own;
}
