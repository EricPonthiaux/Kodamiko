// SPDX-License-Identifier: GPL-3.0-only

#include "diagnostics/JsonSchema.h"
#include "diagnostics/JsonTree.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

class TestDiagnostics : public QObject {
    Q_OBJECT

private slots:
    void parsesWithPositions();
    void decodesEscapes();
    void reportsSyntaxErrorsWhereTheyAre();
    void checksTypesEnumsAndPatterns();
    void anyOfSaysWhatItExpected();
    void reportsUnknownAndDuplicateKeys();
    void describesKeys();
    void followsARefIntoAnotherDocument();
    void ignoresARefToAnUnknownDocument();

private:
    static QJsonObject object(const QByteArray &json);
};

void TestDiagnostics::parsesWithPositions()
{
    const QByteArray text = R"({"a": [1, "é", true], "b": {"c": null}})";
    const JsonParseResult result = parseJson(text);
    QVERIFY(result.root);
    const JsonNode &root = *result.root;
    QCOMPARE(root.type, QJsonValue::Object);
    QCOMPARE(root.members.size(), 2);
    const JsonMember &a = root.members[0];
    QCOMPARE(text.mid(a.keyStart, a.keyEnd - a.keyStart), QByteArray("\"a\""));
    QCOMPARE(text.mid(a.value.start, a.value.end - a.value.start), QByteArray(R"([1, "é", true])"));
    QCOMPARE(a.value.items[1].scalar.toString(), QStringLiteral("é"));
    QCOMPARE(root.toJson(), QJsonDocument::fromJson(text).object());
}

void TestDiagnostics::decodesEscapes()
{
    const JsonParseResult result = parseJson(R"(["a\"b\\\né😀"])");
    QVERIFY(result.root);
    QCOMPARE(result.root->items[0].scalar.toString(), QStringLiteral("a\"b\\\né😀"));
}

void TestDiagnostics::reportsSyntaxErrorsWhereTheyAre()
{
    const auto errorAt = [](const QByteArray &text) {
        const JsonParseResult result = parseJson(text);
        return result.root ? -1 : result.errorOffset;
    };
    QCOMPARE(errorAt("{\"a\": 1,}"), 8); // trailing comma: at the "}"
    QCOMPARE(errorAt("{\"a\" 1}"), 5);
    QCOMPARE(errorAt("{\"a\": tru}"), 6);
    QCOMPARE(errorAt("[1 2]"), 3);
    QCOMPARE(errorAt("{\"a\": \"x"), 6);
    QCOMPARE(errorAt(""), 0);
    QCOMPARE(errorAt("{} x"), 3);
    QCOMPARE(errorAt("{\"a\": 01}"), 7);
    QCOMPARE(errorAt("{\"a\": 1}"), -1);
}

void TestDiagnostics::checksTypesEnumsAndPatterns()
{
    const JsonSchema schema(object(R"j({
        "type": "object",
        "properties": {
            "model": {"type": "string", "enum": ["opus", "sonnet"]},
            "days": {"type": "integer", "minimum": 1},
            "allow": {"type": "array", "items": {"$ref": "#/$defs/rule"}, "uniqueItems": true},
            "env": {"type": "object", "propertyNames": {"pattern": "^[A-Z_]+$"}, "additionalProperties": {"type": "string"}}
        },
        "$defs": {"rule": {"type": "string", "pattern": "^(Bash|Read)(\\(.+\\))?$"}}
    })j"));
    const QByteArray text =
        R"({"model": "gpt", "days": 1.5, "allow": ["Read", "Write", "Read"], "env": {"lower": "x", "A": 1}})";
    const Diagnostics diagnostics = schema.validate(*parseJson(text).root);
    const auto at = [&](const QByteArray &fragment) {
        const int start = int(text.indexOf(fragment));
        for (const Diagnostic &diagnostic : diagnostics) {
            if (diagnostic.start == start && diagnostic.end == start + fragment.size())
                return diagnostic.message;
        }
        return QString();
    };
    QVERIFY(at("\"gpt\"").contains("\"opus\", \"sonnet\""));
    QVERIFY(at("1.5").contains("an integer"));
    QVERIFY(at("\"Write\"").contains("expected form"));
    QVERIFY(!at("\"lower\"").isEmpty()); // propertyNames
    const int one = int(text.indexOf("1}"));
    QVERIFY(std::any_of(diagnostics.begin(), diagnostics.end(), [&](const Diagnostic &diagnostic) {
        return diagnostic.start == one && diagnostic.end == one + 1 && diagnostic.message.contains("a string");
    }));
    const int secondRead = int(text.lastIndexOf("\"Read\""));
    QVERIFY(std::any_of(diagnostics.begin(), diagnostics.end(), [&](const Diagnostic &diagnostic) {
        return diagnostic.start == secondRead && diagnostic.message.contains("Already");
    }));
    QCOMPARE(diagnostics.size(), 6);
    for (const Diagnostic &diagnostic : diagnostics)
        QCOMPARE(diagnostic.severity, Diagnostic::Severity::Warning);
}

void TestDiagnostics::anyOfSaysWhatItExpected()
{
    const JsonSchema schema(object(R"j({
        "properties": {
            "theme": {"anyOf": [{"type": "string", "enum": ["dark", "light"]}, {"type": "string", "pattern": "^custom:.+"}]},
            "org": {"oneOf": [{"type": "string"}, {"type": "array", "items": {"type": "string"}}]},
            "hook": {"anyOf": [{"type": "object", "properties": {"type": {"const": "command"}, "command": {"type": "string"}}, "required": ["type", "command"]},
                               {"type": "object", "properties": {"type": {"const": "http"}, "url": {"type": "string"}}, "required": ["type", "url"]}]}
        }
    })j"));
    const QByteArray text = R"({"theme": "blue", "org": 3, "hook": {"type": "command"}, "ok": {"theme": 1}})";
    const Diagnostics diagnostics = schema.validate(*parseJson(text).root);
    QStringList messages;
    for (const Diagnostic &diagnostic : diagnostics)
        messages.append(diagnostic.message);
    QVERIFY2(messages.size() == 3, qPrintable(messages.join(" | ")));
    QVERIFY(messages[0].contains("custom:")); // both branches are strings: the closest one's details
    QCOMPARE(messages[1], QStringLiteral("Expected a string or an array, not a number"));
    QVERIFY(messages[2].contains("command")); // the branch that fits best: one key missing
}

void TestDiagnostics::reportsUnknownAndDuplicateKeys()
{
    const JsonSchema schema(object(R"j({
        "properties": {
            "model": {"type": "string"},
            "permissions": {"type": "object", "properties": {"allow": {"type": "array"}}, "additionalProperties": false}
        }
    })j"));
    const QByteArray text =
        R"({"modle": "x", "custom": 1, "permissions": {"alow": [], "zzz": 1}, "model": "a", "model": "b"})";
    const Diagnostics diagnostics = schema.validate(*parseJson(text).root);
    QStringList messages;
    for (const Diagnostic &diagnostic : diagnostics)
        messages.append(diagnostic.message);
    QVERIFY2(messages.size() == 4, qPrintable(messages.join(" | ")));
    QVERIFY(messages[0].contains("did you mean “model”")); // open object: only close names
    QVERIFY(messages[1].contains("did you mean “allow”"));
    QCOMPARE(messages[2], QStringLiteral("Unknown key “zzz”"));
    QVERIFY(messages[3].startsWith("Duplicate key"));
    QCOMPARE(diagnostics[3].start, int(text.indexOf("\"model\"")));
}

void TestDiagnostics::describesKeys()
{
    const JsonSchema schema(object(R"j({
        "properties": {"permissions": {"type": "object", "properties": {"allow": {"type": "array", "items": {"$ref": "#/$defs/rule"}, "description": "Allowed"}}}},
        "$defs": {"rule": {"type": "string", "description": "A rule"}}
    })j"));
    QCOMPARE(schema.description({"permissions", "allow"}), QStringLiteral("Allowed"));
    QCOMPARE(schema.description({"permissions", "allow", "0"}), QStringLiteral("A rule"));
    QCOMPARE(schema.description({"nothing"}), QString());
}

void TestDiagnostics::followsARefIntoAnotherDocument()
{
    const QJsonObject other = object(R"j({
        "definitions": {"entry": {"type": "object", "properties": {"env": {"$ref": "#/definitions/env"}}},
                        "env": {"type": "object", "additionalProperties": {"type": "string"}}}
    })j");
    const JsonSchema schema(object(R"j({"properties": {"entry": {"$ref": "other.json#/definitions/entry"}}})j"),
                            {{"other.json", other}});
    const QByteArray text = R"({"entry": {"env": {"A": 1}}})";

    const Diagnostics diagnostics = schema.validate(*parseJson(text).root);

    // Its own "#/definitions/env" is read in the other document.
    QCOMPARE(diagnostics.size(), 1);
    QCOMPARE(diagnostics[0].message, QStringLiteral("Expected a string, not a number"));
}

void TestDiagnostics::ignoresARefToAnUnknownDocument()
{
    const JsonSchema schema(object(R"j({"properties": {"entry": {"$ref": "other.json#/definitions/entry"}}})j"));

    QVERIFY(schema.validate(*parseJson(R"({"entry": 1})").root).isEmpty());
}

QJsonObject TestDiagnostics::object(const QByteArray &json)
{
    QJsonParseError error;
    const QJsonObject result = QJsonDocument::fromJson(json, &error).object();
    if (error.error != QJsonParseError::NoError)
        qFatal("Invalid test schema: %s", qPrintable(error.errorString()));
    return result;
}

QTEST_GUILESS_MAIN(TestDiagnostics)
#include "tst_diagnostics.moc"
