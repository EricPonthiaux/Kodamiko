// SPDX-License-Identifier: GPL-3.0-only

#include "FindInFiles.h"

#include "editor/TextFile.h"
#include "workspace/ProjectFiles.h"

#include <QDir>
#include <QFile>

#include <functional>

namespace {

constexpr qint64 maxFileSize = qint64{4} * 1024 * 1024;
constexpr qsizetype binaryProbeSize = 8192; // bytes looked at for a NUL to detect binary files

// A file name pattern of FindQuery::fileMask.
struct Mask {
    QRegularExpression wildcard;
    bool exclusion = false;
};

QList<Mask> compileMasks(const QString &fileMask)
{
    QList<Mask> masks;
    for (const QString &part : fileMask.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        QString mask = part.trimmed();
        if (mask.isEmpty())
            continue;
        const bool exclusion = mask.startsWith(QLatin1Char('!'));
        if (exclusion)
            mask.remove(0, 1);
        masks.append({QRegularExpression(QRegularExpression::wildcardToRegularExpression(mask),
                                         QRegularExpression::CaseInsensitiveOption),
                      exclusion});
    }
    return masks;
}

// A search over the files of a folder, run off the UI thread.
struct FolderSearch {
    QString folder;
    FindQuery query;
    QRegularExpression pattern;
    QHash<QString, QString> buffers; // unsaved text of open files, by absolute path
};

struct FolderSearchResult {
    QList<FindMatch> matches;
    bool truncated = false;
    int filesSearched = 0;
};

// The text to search for `path`: the editor's unsaved text, else the file's
// decoded content; nullopt for an unreadable or binary file.
std::optional<QString> searchableText(const QString &path, const QHash<QString, QString> &buffers)
{
    if (const auto buffer = buffers.constFind(path); buffer != buffers.cend())
        return *buffer;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray bytes = file.readAll();
    if (bytes.left(binaryProbeSize).contains('\0'))
        return std::nullopt;
    return TextFile::decode(bytes).text;
}

// Stops at FindInFilesSearch::matchLimit matches, or as soon as `isCancelled` says so.
FolderSearchResult searchFolder(const FolderSearch &search, const std::function<bool()> &isCancelled)
{
    constexpr int matchLimit = FindInFilesSearch::matchLimit;
    FolderSearchResult result;
    ProjectFiles::Options options;
    options.hidden = true;
    options.accept = [&search](const QFileInfo &entry) {
        return entry.size() <= maxFileSize && search.query.acceptsFile(entry.fileName());
    };
    for (const QFileInfo &entry : ProjectFiles::list(search.folder, options)) {
        if (isCancelled())
            break;
        const QString path = entry.absoluteFilePath();
        const std::optional<QString> text = searchableText(path, search.buffers);
        if (!text)
            continue;
        ++result.filesSearched;
        const int remaining = matchLimit - static_cast<int>(result.matches.size());
        result.matches.append(FindInFiles::search(*text, search.pattern, path, remaining + 1));
        if (result.matches.size() > matchLimit) {
            result.matches.resize(matchLimit);
            result.truncated = true;
            break;
        }
    }
    return result;
}

} // namespace

std::optional<QRegularExpression> FindQuery::compile(QString *error) const
{
    if (text.isEmpty()) {
        if (error)
            *error = QString();
        return std::nullopt;
    }
    QString pattern = regularExpression ? text : QRegularExpression::escape(text);
    if (wholeWords)
        pattern = QStringLiteral("\\b(?:%1)\\b").arg(pattern);
    QRegularExpression::PatternOptions options = QRegularExpression::MultilineOption;
    if (!matchCase)
        options |= QRegularExpression::CaseInsensitiveOption;
    QRegularExpression expression(pattern, options);
    if (!expression.isValid()) {
        if (error)
            *error = expression.errorString();
        return std::nullopt;
    }
    return expression;
}

bool FindQuery::acceptsFile(const QString &fileName) const
{
    // Asked for every file of the project: the masks are compiled once per
    // query (and per thread), not once per file.
    thread_local QString compiledFor;
    thread_local QList<Mask> masks;
    if (masks.isEmpty() || compiledFor != fileMask) {
        masks = compileMasks(fileMask);
        compiledFor = fileMask;
    }

    bool hasInclusion = false;
    bool included = false;
    for (const Mask &mask : std::as_const(masks)) {
        const bool exclusion = mask.exclusion;
        const bool matches = mask.wildcard.match(fileName).hasMatch();
        if (exclusion && matches)
            return false;
        if (!exclusion) {
            hasInclusion = true;
            included = included || matches;
        }
    }
    return !hasInclusion || included;
}

namespace FindInFiles {

QList<FindMatch> search(const QString &text, const QRegularExpression &pattern, const QString &filePath, int limit)
{
    QList<FindMatch> matches;
    qsizetype lineStart = 0;
    int line = 0;
    qsizetype counted = 0; // position up to which `line` is known
    QRegularExpressionMatchIterator it = pattern.globalMatch(text);
    while (it.hasNext() && matches.size() < limit) {
        const QRegularExpressionMatch match = it.next();
        if (match.capturedLength() == 0)
            continue;
        const qsizetype start = match.capturedStart();
        for (; counted < start; ++counted) {
            if (text.at(counted) == QLatin1Char('\n')) {
                ++line;
                lineStart = counted + 1;
            }
        }
        qsizetype lineEnd = text.indexOf(QLatin1Char('\n'), start);
        if (lineEnd < 0)
            lineEnd = text.size();
        if (match.capturedEnd() > lineEnd)
            continue; // spans several lines
        QString lineText = text.mid(lineStart, lineEnd - lineStart);
        if (lineText.endsWith(QLatin1Char('\r')))
            lineText.chop(1);
        matches.append(
            {filePath, line, static_cast<int>(start - lineStart), static_cast<int>(match.capturedLength()), lineText});
    }
    return matches;
}

} // namespace FindInFiles

FindInFilesSearch::FindInFilesSearch(QObject *parent)
    : QObject(parent)
    , generation_(std::make_shared<std::atomic_int>(0))
{
    pool_.setMaxThreadCount(1);
}

FindInFilesSearch::~FindInFilesSearch()
{
    cancel();
    pool_.waitForDone();
}

void FindInFilesSearch::cancel()
{
    ++*generation_;
}

void FindInFilesSearch::start(const QString &folder, const FindQuery &query, const QHash<QString, QString> &buffers)
{
    const int generation = ++*generation_;
    const std::optional<QRegularExpression> pattern = query.compile();
    if (!pattern || folder.isEmpty()) {
        emit finished({}, false, 0);
        return;
    }
    ++running_;
    FolderSearch search{folder, query, *pattern, buffers};
    pool_.start([this, search = std::move(search), generation, current = generation_] {
        FolderSearchResult result = searchFolder(search, [&] { return *current != generation; });
        QMetaObject::invokeMethod(
            this,
            [this, result = std::move(result), generation, current] {
                --running_;
                if (*current == generation)
                    emit finished(result.matches, result.truncated, result.filesSearched);
            },
            Qt::QueuedConnection);
    });
}
