#include "LspProtocol.h"

#include <QJsonArray>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace LspProtocol {

QString uriOf(const QString &filePath)
{
    return QUrl::fromLocalFile(filePath).toString();
}

QString pathOf(const QString &uri)
{
    const QUrl url(uri);
    return url.isLocalFile() ? url.toLocalFile() : QString();
}

QJsonObject toJson(const Position &position)
{
    return QJsonObject{{"line", position.line}, {"character", position.character}};
}

QJsonObject toJson(const Range &range)
{
    return QJsonObject{{"start", toJson(range.start)}, {"end", toJson(range.end)}};
}

Position positionFrom(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    return {object.value("line").toInt(), object.value("character").toInt()};
}

Range rangeFrom(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    return {positionFrom(object.value("start")), positionFrom(object.value("end"))};
}

QList<Location> parseLocations(const QJsonValue &result)
{
    QList<Location> locations;
    const auto add = [&locations](const QJsonObject &object) {
        // A LocationLink names its target, a Location its own place.
        const bool link = object.contains("targetUri");
        const QString path = pathOf(object.value(link ? "targetUri" : "uri").toString());
        if (path.isEmpty())
            return;
        locations.append({path, rangeFrom(object.value(link ? "targetSelectionRange" : "range"))});
    };
    if (result.isObject())
        add(result.toObject());
    for (const QJsonValue &item : result.toArray())
        add(item.toObject());
    return locations;
}

WorkspaceEdit parseWorkspaceEdit(const QJsonObject &edit)
{
    WorkspaceEdit files;
    const auto addEdits = [&files](const QString &uri, const QJsonArray &edits) {
        const QString path = pathOf(uri);
        if (path.isEmpty())
            return;
        QList<TextEdit> &list = files[path];
        for (const QJsonValue &item : edits) {
            const QJsonObject object = item.toObject();
            list.append({rangeFrom(object.value("range")), object.value("newText").toString()});
        }
    };
    const QJsonObject changes = edit.value("changes").toObject();
    for (auto it = changes.begin(); it != changes.end(); ++it)
        addEdits(it.key(), it.value().toArray());
    for (const QJsonValue &item : edit.value("documentChanges").toArray()) {
        const QJsonObject change = item.toObject();
        if (change.contains("kind"))
            continue; // create, rename or delete a file
        addEdits(change.value("textDocument").toObject().value("uri").toString(), change.value("edits").toArray());
    }
    return files;
}

qsizetype offsetOf(const QString &text, const Position &position)
{
    qsizetype lineStart = 0;
    for (int line = 0; line < position.line; ++line) {
        const qsizetype end = text.indexOf(QLatin1Char('\n'), lineStart);
        if (end < 0)
            return text.size();
        lineStart = end + 1;
    }
    qsizetype lineEnd = text.indexOf(QLatin1Char('\n'), lineStart);
    if (lineEnd < 0)
        lineEnd = text.size();
    return std::min(lineStart + std::max(position.character, 0), lineEnd);
}

QList<TextEdit> applicationOrder(QList<TextEdit> edits)
{
    // Insertions at the same place keep their order, so the last one goes
    // in first.
    std::stable_sort(edits.begin(), edits.end(),
                     [](const TextEdit &a, const TextEdit &b) { return b.range.start < a.range.start; });
    for (qsizetype first = 0; first < edits.size();) {
        qsizetype last = first + 1;
        while (last < edits.size() && edits[last].range.start == edits[first].range.start)
            ++last;
        std::reverse(edits.begin() + first, edits.begin() + last);
        first = last;
    }
    return edits;
}

QString applyEdits(const QString &text, QList<TextEdit> edits)
{
    QString result = text;
    for (const TextEdit &edit : applicationOrder(std::move(edits))) {
        const qsizetype start = offsetOf(result, edit.range.start);
        const qsizetype end = std::max(start, offsetOf(result, edit.range.end));
        result.replace(start, end - start, edit.newText);
    }
    return result;
}

QString plainText(const QJsonValue &contents)
{
    if (contents.isString())
        return contents.toString();
    if (contents.isObject())
        return contents.toObject().value("value").toString(); // MarkupContent or {language, value}
    QStringList parts;
    for (const QJsonValue &item : contents.toArray()) {
        const QString part = plainText(item);
        if (!part.isEmpty())
            parts.append(part);
    }
    return parts.join(QStringLiteral("\n\n"));
}

} // namespace LspProtocol
