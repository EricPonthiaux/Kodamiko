// SPDX-License-Identifier: GPL-3.0-only

#include "UsageFinder.h"

#include "editor/TextFile.h"
#include "index/OccurrenceScanner.h"
#include "index/OccurrenceStore.h"
#include "syntax/LanguageRegistry.h"

#include <QMap>

#include <algorithm>
#include <optional>

using namespace LspProtocol;

namespace UsageFinder {

namespace {

// A file's text, its lines, and its occurrences by start byte.
struct ScannedFile {
    QByteArray text; // UTF-8
    QList<qsizetype> lineStarts;
    QList<Occurrence> occurrences;
    QHash<std::uint32_t, OccurrenceRole> roles;
};

std::optional<ScannedFile> scan(const QString &path, const QHash<QString, QString> &unsavedTexts)
{
    ScannedFile file;
    if (const auto unsaved = unsavedTexts.constFind(path); unsaved != unsavedTexts.cend()) {
        file.text = unsaved->toUtf8();
    } else {
        QString error;
        const std::optional<DecodedText> decoded = TextFile::load(path, &error);
        if (!decoded)
            return std::nullopt;
        file.text = decoded->text.toUtf8();
    }
    file.lineStarts.append(0);
    for (qsizetype i = 0; i < file.text.size(); ++i) {
        if (file.text[i] == '\n')
            file.lineStarts.append(i + 1);
    }
    thread_local OccurrenceScanner scanner;
    file.occurrences = scanner.scan(LanguageRegistry::languageForFile(path), file.text);
    for (const Occurrence &occurrence : std::as_const(file.occurrences))
        file.roles.insert(occurrence.offset, occurrence.role);
    return file;
}

QByteArrayView lineOf(const ScannedFile &file, int line)
{
    const qsizetype start = file.lineStarts.value(line, file.text.size());
    qsizetype end = file.lineStarts.value(line + 1, file.text.size() + 1) - 1;
    end = std::clamp(end, start, file.text.size());
    QByteArrayView text(file.text.constData() + start, end - start);
    if (text.endsWith('\r'))
        text.chop(1);
    return text;
}

// The byte offset of `position` in `file`.
std::uint32_t offsetOf(const ScannedFile &file, const Position &position)
{
    const QByteArrayView line = lineOf(file, position.line);
    const QString lineText = QString::fromUtf8(line);
    const qsizetype bytes = lineText.left(std::max(position.character, 0)).toUtf8().size();
    return std::uint32_t(file.lineStarts.value(position.line, 0) + bytes);
}

Usage usageAt(const QString &path, const ScannedFile &file, const Range &range, OccurrenceRole role)
{
    const int length = range.end.line == range.start.line ? range.end.character - range.start.character : 0;
    return {path,
            range.start.line,
            range.start.character,
            std::max(length, 0),
            QString::fromUtf8(lineOf(file, range.start.line)),
            kindOf(role)};
}

bool isBefore(const Usage &a, const Usage &b)
{
    if (a.path != b.path)
        return a.path < b.path;
    return std::pair(a.line, a.column) < std::pair(b.line, b.column);
}

} // namespace

UsageKind kindOf(OccurrenceRole role)
{
    switch (role) {
    case OccurrenceRole::Definition:
        return UsageKind::Declaration;
    case OccurrenceRole::Reference:
        return UsageKind::Read;
    case OccurrenceRole::Write:
        return UsageKind::Write;
    case OccurrenceRole::Call:
        return UsageKind::Call;
    case OccurrenceRole::Import:
        return UsageKind::Import;
    case OccurrenceRole::Comment:
    case OccurrenceRole::String:
    case OccurrenceRole::Text:
        return UsageKind::Textual;
    }
    return UsageKind::Read;
}

QList<Usage> classify(const QList<Location> &locations, const QHash<QString, QString> &unsavedTexts)
{
    QMap<QString, QList<Range>> byFile;
    for (const Location &location : locations)
        byFile[location.filePath].append(location.range);
    QList<Usage> usages;
    for (auto it = byFile.cbegin(); it != byFile.cend(); ++it) {
        const std::optional<ScannedFile> file = scan(it.key(), unsavedTexts);
        if (!file)
            continue;
        for (const Range &range : it.value()) {
            const OccurrenceRole role = file->roles.value(offsetOf(*file, range.start), OccurrenceRole::Reference);
            usages.append(usageAt(it.key(), *file, range, role));
        }
    }
    std::ranges::sort(usages, isBefore);
    return usages;
}

QList<Usage> findWord(const QString &word, const OccurrenceStore *index, const QHash<QString, QString> &unsavedTexts)
{
    const QByteArray bytes = word.toUtf8();
    QStringList files = index ? index->filesContaining(bytes) : QStringList();
    for (auto it = unsavedTexts.cbegin(); it != unsavedTexts.cend(); ++it) {
        if (!files.contains(it.key()))
            files.append(it.key());
    }
    QList<Usage> usages;
    for (const QString &path : std::as_const(files)) {
        const std::optional<ScannedFile> file = scan(path, unsavedTexts);
        if (!file)
            continue;
        for (const Occurrence &occurrence : file->occurrences) {
            if (occurrence.word != bytes)
                continue;
            const int line = int(occurrence.line);
            const QByteArrayView before(file->text.constData() + file->lineStarts.value(line),
                                        qsizetype(occurrence.offset) - file->lineStarts.value(line));
            const int column = int(QString::fromUtf8(before).size());
            usages.append(usageAt(path, *file, {{line, column}, {line, column + int(word.size())}}, occurrence.role));
        }
    }
    std::ranges::sort(usages, isBefore);
    return usages;
}

} // namespace UsageFinder
