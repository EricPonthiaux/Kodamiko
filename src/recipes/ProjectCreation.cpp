// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectCreation.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QObject>

namespace {

bool writeFile(const QString &path, const QByteArray &content, QString *error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = QObject::tr("Cannot create the folder %1").arg(QDir::toNativeSeparators(QFileInfo(path).path()));
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        *error = QObject::tr("Cannot create %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    file.write(content);
    return true;
}

// `text` with each {{name}} of `placeholders` replaced.
QString filled(QString text, const QMap<QString, QString> &placeholders)
{
    for (auto it = placeholders.cbegin(); it != placeholders.cend(); ++it)
        text.replace(QStringLiteral("{{%1}}").arg(it.key()), it.value());
    return text;
}

// `content` with the placeholders replaced; a binary file (an icon) as is.
QByteArray filled(const QByteArray &content, const QMap<QString, QString> &placeholders)
{
    if (content.contains('\0'))
        return content;
    return filled(QString::fromUtf8(content), placeholders).toUtf8();
}

// Copies the files of `source` into `folder`, the placeholders replaced in
// their paths and texts.
bool copyFiles(const QString &source, const QString &folder, const QMap<QString, QString> &placeholders, QString *error)
{
    QDirIterator files(source, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString path = files.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QObject::tr("Cannot read %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
            return false;
        }
        const QString relative = filled(QDir(source).relativeFilePath(path), placeholders);
        if (!writeFile(QDir(folder).filePath(relative), filled(file.readAll(), placeholders), error))
            return false;
    }
    return true;
}

} // namespace

namespace ProjectCreation {

bool isFreeFolder(const QString &folder)
{
    const QFileInfo info(folder);
    if (!info.exists())
        return true;
    return info.isDir() && QDir(folder).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
}

QString identifierOf(const QString &name)
{
    QString identifier;
    for (const QChar c : name.toLower()) {
        const bool isKept = c.unicode() < 128 && c.isLetterOrNumber();
        if (isKept)
            identifier.append(c);
        else if (!identifier.endsWith(QLatin1Char('_')))
            identifier.append(QLatin1Char('_'));
    }
    while (identifier.startsWith(QLatin1Char('_')))
        identifier.remove(0, 1);
    while (identifier.endsWith(QLatin1Char('_')))
        identifier.chop(1);
    if (identifier.isEmpty())
        return QStringLiteral("project");
    // npm refuses a name that starts with "_", Python one with a digit.
    if (identifier.front().isDigit())
        identifier.prepend(QStringLiteral("project_"));
    return identifier;
}

bool create(const Recipe &given, const QString &folder, QString *error)
{
    // The values given, else the parameters' first ones.
    const Recipe recipe = given.withValues(given.values);
    if (!isFreeFolder(folder)) {
        *error = QObject::tr("%1 already exists and is not an empty folder").arg(QDir::toNativeSeparators(folder));
        return false;
    }
    if (!QDir().mkpath(folder)) {
        *error = QObject::tr("Cannot create the folder %1").arg(QDir::toNativeSeparators(folder));
        return false;
    }
    const QString identifier = identifierOf(QFileInfo(folder).fileName());
    // {{identifier}} and the parameters' values ({{angular}}).
    QMap<QString, QString> placeholders = recipe.values;
    placeholders.insert(QStringLiteral("identifier"), identifier);
    if (!copyFiles(QDir(recipe.folder).filePath(QStringLiteral("files")), folder, placeholders, error))
        return false;
    const QString runFile = RunConfigurations::filePath(folder);
    if (recipe.runConfigurations.isEmpty() || QFileInfo::exists(runFile))
        return true;
    return writeFile(runFile, RunConfigurations::serialize(recipe.runConfigurationsFor(identifier)), error);
}

} // namespace ProjectCreation
