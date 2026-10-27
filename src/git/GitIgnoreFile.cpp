// SPDX-License-Identifier: GPL-3.0-only

#include "GitIgnoreFile.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QSaveFile>

namespace {

// Characters that make a .gitignore line a wildcard pattern.
const QString wildcardCharacters = QStringLiteral("\\*?[");

QString escaped(const QString &path)
{
    QString result;
    result.reserve(path.size());
    for (const QChar character : path) {
        if (wildcardCharacters.contains(character))
            result += QLatin1Char('\\');
        result += character;
    }
    // git drops trailing spaces unless they are escaped.
    qsizetype trailingSpaces = 0;
    while (trailingSpaces < result.size() && result.at(result.size() - 1 - trailingSpaces) == QLatin1Char(' '))
        ++trailingSpaces;
    if (trailingSpaces > 0)
        result.insert(result.size() - trailingSpaces, QLatin1Char('\\'));
    return result;
}

} // namespace

namespace GitIgnoreFile {

QString patternFor(const QString &repositoryPath, EntryKind kind)
{
    QString pattern = QLatin1Char('/') + escaped(repositoryPath);
    if (kind == EntryKind::Folder)
        pattern += QLatin1Char('/');
    return pattern;
}

bool add(const QString &workDir, const QStringList &patterns, QString *error)
{
    const QString path = QDir(workDir).filePath(QStringLiteral(".gitignore"));
    QByteArray content;
    if (QFile existing(path); existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly)) {
            *error = QObject::tr("Cannot read %1: %2").arg(QDir::toNativeSeparators(path), existing.errorString());
            return false;
        }
        content = existing.readAll();
    }
    const QByteArray newline = content.contains("\r\n") ? QByteArrayLiteral("\r\n") : QByteArrayLiteral("\n");
    QList<QByteArray> lines = content.split('\n');
    for (QByteArray &line : lines)
        line = line.trimmed();

    QByteArray added;
    for (const QString &pattern : patterns) {
        const QByteArray line = pattern.toUtf8();
        if (lines.contains(line))
            continue;
        lines.append(line);
        added += line + newline;
    }
    if (added.isEmpty())
        return true;
    if (!content.isEmpty() && !content.endsWith('\n'))
        content += newline;

    QSaveFile file(path);
    const bool isWritten = file.open(QIODevice::WriteOnly) && file.write(content + added) >= 0 && file.commit();
    if (!isWritten)
        *error = QObject::tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
    return isWritten;
}

} // namespace GitIgnoreFile
