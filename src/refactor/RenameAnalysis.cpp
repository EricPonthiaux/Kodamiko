// SPDX-License-Identifier: GPL-3.0-only

#include "RenameAnalysis.h"

#include "editor/TextFile.h"
#include "index/OccurrenceScanner.h"
#include "index/OccurrenceStore.h"
#include "syntax/LanguageRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <optional>
#include <variant>

using namespace LspProtocol;

namespace RenameAnalysis {

namespace {

// The occurrences that a rename in comments and strings replaces.
constexpr std::initializer_list<OccurrenceRole> textualRoles = {OccurrenceRole::Comment, OccurrenceRole::String,
                                                                OccurrenceRole::Text};

// A file's text, read once, with where its lines start.
struct ReadFile {
    QByteArray text; // UTF-8
    QList<qsizetype> lineStarts;
};

QString tr(const char *text)
{
    return QCoreApplication::translate("RenameAnalysis", text);
}

QString nameOf(const QString &path)
{
    return QFileInfo(path).fileName();
}

std::optional<ReadFile> readFile(const QString &path, const QHash<QString, QString> &unsavedTexts)
{
    ReadFile file;
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
    return file;
}

QList<Occurrence> occurrencesOf(const QString &path, const ReadFile &file)
{
    thread_local OccurrenceScanner scanner;
    return scanner.scan(LanguageRegistry::languageForFile(path), file.text);
}

// The ranges the server's edit changes in `path`.
QList<Range> codeRanges(const WorkspaceEdit &edit, const QString &path)
{
    QList<Range> ranges;
    for (const DocumentChange &change : edit.changes) {
        const auto *documentEdit = std::get_if<DocumentEdit>(&change);
        if (!documentEdit || documentEdit->filePath != path)
            continue;
        for (const TextEdit &textEdit : documentEdit->edits)
            ranges.append(textEdit.range);
    }
    return ranges;
}

bool overlaps(const Range &range, const QList<Range> &others)
{
    return std::ranges::any_of(
        others, [&range](const Range &other) { return range.start < other.end && other.start < range.end; });
}

// The name in the comments, strings or text of `path`, replaced.
QList<TextEdit> textualEditsIn(const QString &path, const ReadFile &file, const Input &input)
{
    const QByteArray name = input.name.toUtf8();
    const QList<Range> code = codeRanges(input.codeEdit, path);
    QList<TextEdit> edits;
    for (const Occurrence &occurrence : occurrencesOf(path, file)) {
        const bool isTextual = std::ranges::find(textualRoles, occurrence.role) != textualRoles.end();
        if (!isTextual || occurrence.word != name)
            continue;
        const int line = int(occurrence.line);
        const qsizetype lineStart = file.lineStarts.value(line);
        const Position start = positionOf(file.text, lineStart, occurrence.offset, line);
        const Range range{start, {line, start.character + int(input.name.size())}};
        if (!overlaps(range, code))
            edits.append({range, input.newName});
    }
    return edits;
}

// The files where the name may occur: the index's, and the unsaved texts.
QStringList candidateFiles(const Input &input)
{
    QStringList files = input.index ? input.index->filesContaining(input.name.toUtf8()) : QStringList();
    for (auto it = input.unsavedTexts.cbegin(); it != input.unsavedTexts.cend(); ++it) {
        if (!files.contains(it.key()))
            files.append(it.key());
    }
    files.sort();
    return files;
}

WorkspaceEdit textualEdit(const Input &input)
{
    WorkspaceEdit edit;
    for (const QString &path : candidateFiles(input)) {
        const std::optional<ReadFile> file = readFile(path, input.unsavedTexts);
        if (!file)
            continue;
        const QList<TextEdit> edits = textualEditsIn(path, *file, input);
        if (!edits.isEmpty())
            edit.changes.append(DocumentEdit{path, std::nullopt, edits});
    }
    return edit;
}

// Where the new name is already declared in `path`, as conflicts.
QStringList declarationsOfNewName(const QString &path, const Input &input)
{
    const std::optional<ReadFile> file = readFile(path, input.unsavedTexts);
    if (!file)
        return {};
    const QByteArray newName = input.newName.toUtf8();
    QStringList conflicts;
    for (const Occurrence &occurrence : occurrencesOf(path, *file)) {
        if (occurrence.word == newName && occurrence.role == OccurrenceRole::Definition)
            conflicts.append(
                tr("“%1” is already declared in %2:%3").arg(input.newName, nameOf(path)).arg(occurrence.line + 1));
    }
    return conflicts;
}

QStringList conflictsOf(const Input &input, const QStringList &changedFiles)
{
    QStringList conflicts;
    const QString projectPrefix = QDir::cleanPath(input.projectFolder) + QLatin1Char('/');
    for (const QString &path : changedFiles) {
        const QFileInfo info(path);
        if (info.exists() && !info.isWritable())
            conflicts.append(tr("%1 is read-only").arg(info.fileName()));
        if (!input.projectFolder.isEmpty() && !path.startsWith(projectPrefix))
            conflicts.append(tr("%1 is outside the project").arg(QDir::toNativeSeparators(path)));
        conflicts.append(declarationsOfNewName(path, input));
    }
    return conflicts;
}

} // namespace

Position positionOf(const QByteArray &text, qsizetype lineStart, std::uint32_t offset, int line)
{
    const QByteArrayView before(text.constData() + lineStart, qsizetype(offset) - lineStart);
    return {line, int(QString::fromUtf8(before).size())};
}

Result analyze(const Input &input)
{
    Result result;
    if (input.searchesCommentsAndStrings)
        result.textualEdit = textualEdit(input);
    QStringList changed = input.codeEdit.filePaths();
    for (const QString &path : result.textualEdit.filePaths()) {
        if (!changed.contains(path))
            changed.append(path);
    }
    result.conflicts = conflictsOf(input, changed);
    return result;
}

} // namespace RenameAnalysis
