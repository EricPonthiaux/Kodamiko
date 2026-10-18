#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

#include <utility>

// Shared helpers of the codecs, ported from sidecar's fence/util.js: source
// cleanup (comments, presentation-only statements), label escaping, and
// stable key generation for serialization.
namespace diagram::source {

// --- Mermaid

// Lines of a Mermaid source, `%%` comments removed; `keepIndent` keeps the
// leading spaces (mindmap hierarchy).
QStringList mermaidLines(const QString &source, bool keepIndent = false);
// A label, always quoted so that any punctuation round-trips.
QString mermaidQuote(const QString &text);
// The final text of a label: <br> and \n become line breaks, HTML and
// Mermaid entities are resolved.
QString decodeLabel(const QString &text);
// A label without its quotes (and markdown string backquotes), decoded.
QString mermaidUnquote(const QString &text);
// An edge's pipe label: no structural pipe inside.
QString pipeLabel(const QString &text);

// --- PlantUML

// PlantUML text: the \n escape is a line break.
QString plantumlText(const QString &text);
// Lines of a PlantUML source: comments, @startuml/@enduml, skinparam and
// presentation-only statements removed.
QStringList plantumlLines(const QString &source);
// A name, quoted when it needs it.
QString plantumlName(const QString &text);

// --- UML relations, shared by the Mermaid and PlantUML class codecs.

struct UmlRelation {
    QString token;
    QString kind;
    bool targetOnLeft; // the semantic target is written on the left
};

const QList<UmlRelation> &umlRelations();
// An alternation of the relation tokens, longest first, for a regex.
QString umlRelationPattern();
// The token written for a relation kind.
QString umlRelationOutput(const QString &kind);

// --- Keys

// A stable source id: the shape's key when it has one, else a slug of its
// text, else "n"; unique within `used`, which the serializer threads through
// the whole diagram.
QString keyFor(const QString &key, const QString &text, QSet<QString> &used);
// Splits on `separator` outside of brackets and quotes.
QStringList splitTop(const QString &text, QChar separator);

} // namespace diagram::source
