#pragma once

#include <QByteArrayView>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <optional>

struct JsonMember;

// A JSON value and where it is in the text, for messages that point at it.
struct JsonNode {
    QJsonValue::Type type = QJsonValue::Null;
    // UTF-8 byte offsets [start, end[ of the value in the text.
    int start = 0;
    int end = 0;
    QJsonValue scalar;         // Bool, Double, String, Null
    QList<JsonMember> members; // Object, in the text's order, duplicates included
    QList<JsonNode> items;     // Array

    // The value without positions; for an object with a duplicated key,
    // the last one wins.
    QJsonValue toJson() const;
};

struct JsonMember {
    QString key;
    int keyStart = 0; // the key's quotes included
    int keyEnd = 0;
    JsonNode value;
};

struct JsonParseResult {
    std::optional<JsonNode> root;
    // When `root` is empty: why, and the byte offset where it went wrong.
    QString error;
    int errorOffset = 0;
};

// Strict JSON (RFC 8259): no comments, no trailing commas.
JsonParseResult parseJson(QByteArrayView utf8);
