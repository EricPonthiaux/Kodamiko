// SPDX-License-Identifier: GPL-3.0-only

#include "FileReferences.h"

#include "editor/TextFile.h"
#include "index/OccurrenceScanner.h"
#include "index/OccurrenceStore.h"
#include "syntax/LanguageRegistry.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <optional>

using namespace LspProtocol;

namespace FileReferences {

namespace {

// How a language spells its references to files.
enum class Family {
    C,      // #include "dir/file.h", from its folder, then from the roots
    Script, // import … from './dir/file', from its folder only
    Python, // import package.module, from the roots
    Other,
};

// A reference to a file, as it is written in a text (UTF-16 positions).
struct Reference {
    qsizetype start = 0;
    QString spec;
    // Python "import a.b" without "as": the code then says "a.b.f()".
    bool isPlainImport = false;
};

// What a reference pointed to before the move, and from where.
struct Target {
    QString path; // before the move
    QString base; // the folder the spec is relative to
    // Script: what the spec left out ("" for none, ".ts", "/index.ts").
    QString completion;
};

// The moves, both ways, and where references are looked up from.
struct Moves {
    QHash<QString, QString> toByFrom;
    QHash<QString, QString> fromByTo;
    QStringList roots;
};

const QStringList scriptCompletions = {QString(),
                                       QStringLiteral(".ts"),
                                       QStringLiteral(".tsx"),
                                       QStringLiteral(".js"),
                                       QStringLiteral(".jsx"),
                                       QStringLiteral(".mjs"),
                                       QStringLiteral(".cjs"),
                                       QStringLiteral("/index.ts"),
                                       QStringLiteral("/index.js")};

// Where the file that was at `path` is now.
QString now(const Moves &moves, const QString &path)
{
    return moves.toByFrom.value(path, path);
}

// Where the file now at `path` was.
QString before(const Moves &moves, const QString &path)
{
    return moves.fromByTo.value(path, path);
}

// Whether a file was at `path` before the moves.
bool existedBefore(const Moves &moves, const QString &path)
{
    return moves.toByFrom.contains(path) || (QFileInfo::exists(path) && !moves.fromByTo.contains(path));
}

Family familyOf(const QString &path)
{
    const QString language = LanguageRegistry::languageForFile(path);
    if (language == QLatin1String("c") || language == QLatin1String("cpp"))
        return Family::C;
    if (language == QLatin1String("javascript") || language == QLatin1String("typescript") ||
        language == QLatin1String("tsx"))
        return Family::Script;
    if (language == QLatin1String("python"))
        return Family::Python;
    return Family::Other;
}

QList<Reference> matchesOf(const QString &text, const QRegularExpression &pattern, int group)
{
    QList<Reference> references;
    QRegularExpressionMatchIterator it = pattern.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        references.append({match.capturedStart(group), match.captured(group)});
    }
    return references;
}

QList<Reference> pythonReferences(const QString &text)
{
    static const QRegularExpression fromImport(QStringLiteral(R"(^[ \t]*from[ \t]+([A-Za-z_][\w.]*)[ \t]+import)"),
                                               QRegularExpression::MultilineOption);
    static const QRegularExpression importList(QStringLiteral(R"(^[ \t]*import[ \t]+([^\n#;]+))"),
                                               QRegularExpression::MultilineOption);
    static const QRegularExpression module(QStringLiteral(R"(([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)([ \t]+as[ \t]+\w+)?)"));
    QList<Reference> references = matchesOf(text, fromImport, 1);
    for (const Reference &list : matchesOf(text, importList, 1)) {
        QRegularExpressionMatchIterator names = module.globalMatch(list.spec);
        while (names.hasNext()) {
            const QRegularExpressionMatch name = names.next();
            references.append({list.start + name.capturedStart(1), name.captured(1), !name.hasCaptured(2)});
        }
    }
    return references;
}

QList<Reference> referencesIn(const QString &text, Family family)
{
    static const QRegularExpression include(QStringLiteral(R"re(^[ \t]*#[ \t]*include[ \t]*"([^"\n]+)")re"),
                                            QRegularExpression::MultilineOption);
    static const QRegularExpression scriptImport(
        QStringLiteral(R"(\b(?:from|import|require)\s*\(?\s*(['"])(\.\.?/[^'"\n]+)\1)"));
    switch (family) {
    case Family::C:
        return matchesOf(text, include, 1);
    case Family::Script:
        return matchesOf(text, scriptImport, 2);
    case Family::Python:
        return pythonReferences(text);
    case Family::Other:
        break;
    }
    return {};
}

QString joined(const QString &base, const QString &relative)
{
    return QDir::cleanPath(base + QLatin1Char('/') + relative);
}

bool isUnder(const QString &path, const QString &folder)
{
    return path.startsWith(folder + QLatin1Char('/'));
}

std::optional<Target> resolveInclude(const QString &spec, const QString &folder, const Moves &moves)
{
    for (const QString &base : QStringList{folder} + moves.roots) {
        const QString path = joined(base, spec);
        if (existedBefore(moves, path))
            return Target{path, base, {}};
    }
    return std::nullopt;
}

std::optional<Target> resolveScript(const QString &spec, const QString &folder, const Moves &moves)
{
    for (const QString &completion : scriptCompletions) {
        const QString path = joined(folder, spec + completion);
        if (existedBefore(moves, path) && !QFileInfo(now(moves, path)).isDir())
            return Target{path, folder, completion};
    }
    return std::nullopt;
}

std::optional<Target> resolvePython(const QString &spec, const Moves &moves)
{
    const QString relative = QString(spec).replace(QLatin1Char('.'), QLatin1Char('/'));
    for (const QString &root : moves.roots) {
        for (const QString &file : {relative + QStringLiteral(".py"), relative + QStringLiteral("/__init__.py")}) {
            const QString path = joined(root, file);
            if (existedBefore(moves, path))
                return Target{path, root, {}};
        }
    }
    return std::nullopt;
}

std::optional<Target> resolve(Family family, const QString &spec, const QString &folder, const Moves &moves)
{
    switch (family) {
    case Family::C:
        return resolveInclude(spec, folder, moves);
    case Family::Script:
        return resolveScript(spec, folder, moves);
    case Family::Python:
        return resolvePython(spec, moves);
    case Family::Other:
        break;
    }
    return std::nullopt;
}

QString includeSpec(const Target &target, const QString &targetNow, const QString &oldFolder, const QString &newFolder)
{
    const bool fromRoot = target.base != oldFolder && isUnder(targetNow, target.base);
    return QDir(fromRoot ? target.base : newFolder).relativeFilePath(targetNow);
}

QString scriptSpec(const Target &target, const QString &targetNow, const QString &newFolder)
{
    QString spec = QDir(newFolder).relativeFilePath(targetNow);
    const QFileInfo moved(targetNow);
    const bool viaIndex = target.completion.startsWith(QLatin1Char('/'));
    if (viaIndex && moved.fileName() == QFileInfo(target.path).fileName())
        spec = QDir(newFolder).relativeFilePath(moved.absolutePath()); // the folder, as before
    else if (!target.completion.isEmpty())
        spec.chop(moved.suffix().size() + 1); // the extension stays left out
    if (!spec.startsWith(QLatin1Char('.')))
        spec.prepend(QStringLiteral("./"));
    return spec;
}

std::optional<QString> pythonSpec(const Target &target, const QString &targetNow)
{
    if (!isUnder(targetNow, target.base))
        return std::nullopt;
    QString module = QDir(target.base).relativeFilePath(targetNow);
    if (module.endsWith(QLatin1String("/__init__.py")))
        module.chop(int(QStringLiteral("/__init__.py").size()));
    else
        module.chop(int(QStringLiteral(".py").size()));
    return module.replace(QLatin1Char('/'), QLatin1Char('.'));
}

// The spec that points from `path` (now) to what `target` pointed to.
std::optional<QString> respell(Family family, const Target &target, const QString &path, const Moves &moves)
{
    const QString targetNow = now(moves, target.path);
    const QString oldFolder = QFileInfo(before(moves, path)).absolutePath();
    const QString newFolder = QFileInfo(path).absolutePath();
    switch (family) {
    case Family::C:
        return includeSpec(target, targetNow, oldFolder, newFolder);
    case Family::Script:
        return scriptSpec(target, targetNow, newFolder);
    case Family::Python:
        return pythonSpec(target, targetNow);
    case Family::Other:
        break;
    }
    return std::nullopt;
}

Position positionIn(const QString &text, qsizetype index)
{
    const qsizetype lineStart = text.lastIndexOf(QLatin1Char('\n'), index - 1) + 1;
    return {int(text.left(index).count(QLatin1Char('\n'))), int(index - lineStart)};
}

// Where the code of a Python `text` names module `spec` ("spec.f()",
// "m = spec"): the start of each, comments and strings left out.
QList<qsizetype> usesOf(const QString &text, const QString &spec)
{
    const QRegularExpression use(QStringLiteral("(?<![\\w.])%1(?!\\w)").arg(QRegularExpression::escape(spec)));
    const QByteArray bytes = text.toUtf8();
    thread_local OccurrenceScanner scanner;
    QSet<std::uint32_t> codeOffsets;
    for (const Occurrence &occurrence : scanner.scan(QStringLiteral("python"), bytes)) {
        const bool isCode = occurrence.role == OccurrenceRole::Reference || occurrence.role == OccurrenceRole::Call ||
                            occurrence.role == OccurrenceRole::Write;
        if (isCode)
            codeOffsets.insert(occurrence.offset);
    }
    QList<qsizetype> uses;
    QRegularExpressionMatchIterator it = use.globalMatch(text);
    while (it.hasNext()) {
        const qsizetype start = it.next().capturedStart();
        if (codeOffsets.contains(std::uint32_t(text.left(start).toUtf8().size())))
            uses.append(start);
    }
    return uses;
}

TextEdit replacement(const QString &text, qsizetype start, qsizetype length, const QString &newText)
{
    const Position from = positionIn(text, start);
    return {{from, {from.line, from.character + int(length)}}, newText};
}

// The code's uses of a module imported by `reference` ("import a.b"),
// renamed `spec`, the import itself left out.
QList<TextEdit> usesRenamed(const QString &text, const Reference &reference, const QString &spec)
{
    QList<TextEdit> edits;
    for (const qsizetype use : usesOf(text, reference.spec)) {
        const bool isTheImport = use >= reference.start && use < reference.start + reference.spec.size();
        if (!isTheImport)
            edits.append(replacement(text, use, reference.spec.size(), spec));
    }
    return edits;
}

// The edits of `path` (where it is now), whose text is `text`.
QList<TextEdit> editsIn(const QString &path, const QString &text, const Moves &moves)
{
    const Family family = familyOf(path);
    const QString oldFolder = QFileInfo(before(moves, path)).absolutePath();
    const bool hasMoved = moves.fromByTo.contains(path);
    QList<TextEdit> edits;
    for (const Reference &reference : referencesIn(text, family)) {
        const std::optional<Target> target = resolve(family, reference.spec, oldFolder, moves);
        // A reference from the file's own folder changes when the file moves.
        const bool mayChange =
            target && (moves.toByFrom.contains(target->path) || (hasMoved && target->base == oldFolder));
        if (!mayChange)
            continue;
        const std::optional<QString> spec = respell(family, *target, path, moves);
        if (!spec || *spec == reference.spec)
            continue;
        edits.append(replacement(text, reference.start, reference.spec.size(), *spec));
        if (reference.isPlainImport)
            edits.append(usesRenamed(text, reference, *spec));
    }
    return edits;
}

Moves movesOf(const Input &input)
{
    Moves moves;
    for (const Move &move : input.moves) {
        moves.toByFrom.insert(QDir::cleanPath(move.from), QDir::cleanPath(move.to));
        moves.fromByTo.insert(QDir::cleanPath(move.to), QDir::cleanPath(move.from));
    }
    if (!input.projectFolder.isEmpty()) {
        const QString root = QDir::cleanPath(input.projectFolder);
        moves.roots = {root, root + QStringLiteral("/src")};
    }
    return moves;
}

// The words a reference to `move`'s file holds: its name, and the
// folders that change ("pkg" of "pkg/a.py" moved to "lib/pkg/a.py"), or
// the folder an index file stands for ("models" of "models/index.ts").
QStringList wordsNaming(const Move &move)
{
    const QFileInfo from(move.from);
    QStringList words{from.baseName()};
    const QStringList fromParts = QDir::cleanPath(move.from).split(QLatin1Char('/'));
    const QStringList toParts = QDir::cleanPath(move.to).split(QLatin1Char('/'));
    qsizetype common = 0;
    while (common < fromParts.size() && common < toParts.size() && fromParts[common] == toParts[common])
        ++common;
    for (qsizetype i = common; i + 1 < fromParts.size(); ++i)
        words.append(fromParts[i]);
    const bool standsForItsFolder =
        from.baseName() == QLatin1String("index") || from.baseName() == QLatin1String("__init__");
    if (standsForItsFolder)
        words.append(from.absoluteDir().dirName());
    return words;
}

// The files that may refer to the moved ones, where they are now.
QStringList candidatesOf(const Input &input, const Moves &moves)
{
    QSet<QString> candidates;
    for (const Move &move : input.moves) {
        candidates.insert(QDir::cleanPath(move.to));
        if (!input.index)
            continue;
        for (const QString &word : wordsNaming(move)) {
            for (const QString &path : input.index->filesContaining(word.toUtf8()))
                candidates.insert(now(moves, path)); // the index may not know the move yet
        }
    }
    for (auto it = input.unsavedTexts.cbegin(); it != input.unsavedTexts.cend(); ++it)
        candidates.insert(it.key());
    QStringList sorted(candidates.cbegin(), candidates.cend());
    sorted.sort();
    return sorted;
}

std::optional<QString> textOf(const QString &path, const QHash<QString, QString> &unsavedTexts)
{
    if (const auto unsaved = unsavedTexts.constFind(path); unsaved != unsavedTexts.cend())
        return *unsaved;
    QString error;
    std::optional<DecodedText> decoded = TextFile::load(path, &error);
    if (!decoded)
        return std::nullopt;
    return std::move(decoded->text);
}

} // namespace

QList<Move> filesOf(const QString &folder, const QString &to)
{
    QList<Move> moves;
    QDirIterator it(to, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        moves.append({QDir::cleanPath(folder + path.mid(to.size())), path});
    }
    return moves;
}

WorkspaceEdit update(const Input &input)
{
    const Moves moves = movesOf(input);
    WorkspaceEdit edit;
    for (const QString &path : candidatesOf(input, moves)) {
        if (familyOf(path) == Family::Other)
            continue;
        const std::optional<QString> text = textOf(path, input.unsavedTexts);
        if (!text)
            continue;
        const QList<TextEdit> edits = editsIn(path, *text, moves);
        if (!edits.isEmpty())
            edit.changes.append(DocumentEdit{path, std::nullopt, edits});
    }
    return edit;
}

} // namespace FileReferences
