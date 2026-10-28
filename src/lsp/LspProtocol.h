// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>
#include <variant>

// The Language Server Protocol's data, as Kodamiko uses it: positions in lines
// and UTF-16 code units (the encoding Kodamiko asks for), locations, and the
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

// The text edits of one file. `version` is the version of the document the
// server computed them for, when it says: they do not fit another text.
struct DocumentEdit {
    QString filePath;
    std::optional<int> version;
    QList<TextEdit> edits;

    bool operator==(const DocumentEdit &) const = default;
};

// Creating, renaming or deleting a file or folder.
struct FileOperation {
    enum class Kind {
        Create,
        Rename,
        Delete
    };

    Kind kind = Kind::Create;
    QString filePath; // created, deleted, or renamed
    QString newPath;  // Rename: the new path
    // Create and Rename: whether an existing target is replaced, or else
    // left as it is without an error.
    bool overwrite = false;
    bool ignoreIfExists = false;
    // Delete: a folder with its content; no error if there is nothing.
    bool recursive = false;
    bool ignoreIfNotExists = false;

    bool operator==(const FileOperation &) const = default;
};

using DocumentChange = std::variant<DocumentEdit, FileOperation>;

// The changes of a rename or a quick fix, in the order to make them.
struct WorkspaceEdit {
    QList<DocumentChange> changes;

    bool isEmpty() const { return changes.isEmpty(); }

    // Every file the edit touches, renamed files under both paths, in the
    // order of the changes, without duplicates.
    QStringList filePaths() const;
    // The number of text edits, file operations not included.
    int textEditCount() const;
};

QString uriOf(const QString &filePath);
QString pathOf(const QString &uri); // empty if not a file URI

QJsonObject toJson(const Position &position);
QJsonObject toJson(const Range &range);
Position positionFrom(const QJsonValue &value);
Range rangeFrom(const QJsonValue &value);

// Result of "definition", "references"...: a Location, a list of them, or
// a list of LocationLinks. Locations outside files are left out.
QList<Location> parseLocations(const QJsonValue &result);

// "documentChanges", with their file operations, or else "changes". Files
// outside the file system are left out, and so are edits without changes.
WorkspaceEdit parseWorkspaceEdit(const QJsonObject &edit);

// UTF-16 offset of `position` in `text`, lines split at '\n' (a "\r"
// before it belongs to the line). Clamped to the line and to the text.
qsizetype offsetOf(const QString &text, const Position &position);

// `edits` in the order to apply them one by one: from the end of the text,
// so that an edit does not move the ones still to apply.
QList<TextEdit> applicationOrder(QList<TextEdit> edits);

// `text` with `edits` applied; they must not overlap.
QString applyEdits(const QString &text, QList<TextEdit> edits);

// The Command of a code action: the action itself when it is a Command,
// else its "command"; empty if there is none.
QJsonObject commandOf(const QJsonObject &action);

// An element of a call or type hierarchy: a function, a method, a type.
// `raw` is the server's object, which the next requests send back.
struct HierarchyItem {
    QString name;
    QString detail; // its signature, its container…
    QString filePath;
    Range selectionRange; // its name
    QJsonObject raw;
};

// The items of a "prepare…Hierarchy", "typeHierarchy/supertypes" or
// "…/subtypes" result; those outside files are left out.
QList<HierarchyItem> parseHierarchyItems(const QJsonValue &result);
// The callers ("callHierarchy/incomingCalls", `key` "from") or the called
// ("…/outgoingCalls", `key` "to") of a call hierarchy result.
QList<HierarchyItem> parseHierarchyCalls(const QJsonValue &result, const QString &key);

// A step of a server's long task (indexing...), from a "$/progress"
// notification of the work done progress protocol.
struct Progress {
    enum class Kind {
        Begin,
        Report,
        End
    };

    QString token; // identifies the task
    Kind kind = Kind::Begin;
    QString title; // in a Begin step only
    QString message;
    std::optional<int> percentage;

    bool operator==(const Progress &) const = default;
};

// The params of "$/progress"; nullopt for another kind of progress
// (partial results) or a malformed one.
std::optional<Progress> parseProgress(const QJsonValue &params);

// The Markdown of a "hover" result's contents: MarkupContent (Markdown, or
// plain text as a code block), MarkedString (a code block when it names its
// language), or a list of MarkedStrings.
QString hoverMarkdown(const QJsonValue &contents);

} // namespace LspProtocol
