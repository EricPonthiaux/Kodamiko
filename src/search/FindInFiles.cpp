#include "FindInFiles.h"

#include "editor/TextFile.h"
#include "workspace/ProjectFiles.h"

#include <QDir>
#include <QFile>

namespace {

constexpr qint64 maxFileSize = 4 * 1024 * 1024;

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
    bool hasInclusion = false;
    bool included = false;
    for (const QString &part : fileMask.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        QString mask = part.trimmed();
        if (mask.isEmpty())
            continue;
        const bool exclusion = mask.startsWith(QLatin1Char('!'));
        if (exclusion)
            mask.remove(0, 1);
        const QRegularExpression wildcard(QRegularExpression::wildcardToRegularExpression(mask),
                                          QRegularExpression::CaseInsensitiveOption);
        const bool matches = wildcard.match(fileName).hasMatch();
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
        matches.append({filePath, line, static_cast<int>(start - lineStart), static_cast<int>(match.capturedLength()),
                        lineText});
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
    pool_.start([this, folder, query, buffers, pattern = *pattern, generation, current = generation_] {
        QList<FindMatch> matches;
        int filesSearched = 0;
        bool truncated = false;
        ProjectFiles::Options options;
        options.hidden = true;
        options.accept = [&query](const QFileInfo &entry) {
            return entry.size() <= maxFileSize && query.acceptsFile(entry.fileName());
        };
        for (const QFileInfo &entry : ProjectFiles::list(folder, options)) {
            if (*current != generation)
                break;
            const QString path = entry.absoluteFilePath();
            QString text;
            if (const auto buffer = buffers.constFind(path); buffer != buffers.cend()) {
                text = *buffer;
            } else {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly))
                    continue;
                const QByteArray bytes = file.readAll();
                if (bytes.left(8192).contains('\0'))
                    continue; // binary
                text = TextFile::decode(bytes).text;
            }
            ++filesSearched;
            const int remaining = matchLimit - static_cast<int>(matches.size());
            matches.append(FindInFiles::search(text, pattern, path, remaining + 1));
            if (matches.size() > matchLimit) {
                matches.resize(matchLimit);
                truncated = true;
                break;
            }
        }
        QMetaObject::invokeMethod(
            this,
            [this, matches = std::move(matches), truncated, filesSearched, generation, current] {
                --running_;
                if (*current == generation)
                    emit finished(matches, truncated, filesSearched);
            },
            Qt::QueuedConnection);
    });
}
