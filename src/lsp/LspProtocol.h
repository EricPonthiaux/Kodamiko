#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QString>

// The Language Server Protocol's data, as Duet uses it: positions in lines
// and UTF-16 code units (the encoding Duet asks for), locations, and the
// edits of a rename or a quick fix.
namespace LspProtocol {

struct Position {
    int line = 0;
    int character = 0; // UTF-16 code units from the start of the line

    bool operator==(const Position &) const = default;
    auto operator<=>(const Position &) const = default;
};

struct Range {
    Position start;
    Position end;

    bool operator==(const Range &) const = default;
};

struct Location {
    QString filePath;
    Range range;

    bool operator==(const Location &) const = default;
};

struct TextEdit {
    Range range;
    QString newText;

    bool operator==(const TextEdit &) const = default;
};

// Edits by file path, as the server sends them for a rename.
using WorkspaceEdit = QMap<QString, QList<TextEdit>>;

QString uriOf(const QString &filePath);
QString pathOf(const QString &uri); // empty if not a file URI

QJsonObject toJson(const Position &position);
QJsonObject toJson(const Range &range);
Position positionFrom(const QJsonValue &value);
Range rangeFrom(const QJsonValue &value);

// Result of "definition", "references"...: a Location, a list of them, or
// a list of LocationLinks. Locations outside files are left out.
QList<Location> parseLocations(const QJsonValue &result);

// "changes" and "documentChanges" (text edits only: creating, renaming
// and deleting files are not supported).
WorkspaceEdit parseWorkspaceEdit(const QJsonObject &edit);

// UTF-16 offset of `position` in `text`, lines split at '\n' (a "\r"
// before it belongs to the line). Clamped to the line and to the text.
qsizetype offsetOf(const QString &text, const Position &position);

// `edits` in the order to apply them one by one: from the end of the text,
// so that an edit does not move the ones still to apply.
QList<TextEdit> applicationOrder(QList<TextEdit> edits);

// `text` with `edits` applied; they must not overlap.
QString applyEdits(const QString &text, QList<TextEdit> edits);

// The Markdown of a "hover" result's contents: MarkupContent (Markdown, or
// plain text as a code block), MarkedString (a code block when it names its
// language), or a list of MarkedStrings.
QString hoverMarkdown(const QJsonValue &contents);

} // namespace LspProtocol
