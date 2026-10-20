// SPDX-License-Identifier: GPL-3.0-only

#include "FileOperations.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace {

QString translate(const char *text)
{
    return QCoreApplication::translate("FileOperations", text);
}

// "Foo.tar.gz" → {"Foo", ".tar.gz"}; a leading dot is part of the base
// (".gitignore" has no extension).
std::pair<QString, QString> splitName(const QString &name)
{
    const qsizetype dot = name.indexOf(QLatin1Char('.'), 1);
    return dot < 0 ? std::pair{name, QString()} : std::pair{name.left(dot), name.mid(dot)};
}

bool copyTree(const QString &source, const QString &destination, QString *error)
{
    const QFileInfo info(source);
    if (info.isSymLink()) {
        // The link itself, not what it points to.
        if (!QFile::link(info.symLinkTarget(), destination)) {
            *error = translate("Cannot copy “%1”").arg(info.fileName());
            return false;
        }
        return true;
    }
    if (!info.isDir()) {
        QFile file(source);
        if (!file.copy(destination)) {
            *error = translate("Cannot copy “%1”: %2").arg(info.fileName(), file.errorString());
            return false;
        }
        return true;
    }
    if (!QDir().mkdir(destination)) {
        *error = translate("Cannot create the folder “%1”").arg(QDir::toNativeSeparators(destination));
        return false;
    }
    const QFileInfoList entries =
        QDir(source).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    return std::ranges::all_of(entries, [&](const QFileInfo &entry) {
        return copyTree(entry.absoluteFilePath(), destination + QLatin1Char('/') + entry.fileName(), error);
    });
}

} // namespace

namespace FileOperations {

bool isInside(const QString &path, const QString &folder)
{
    const QString cleanPath = QDir::cleanPath(path);
    const QString cleanFolder = QDir::cleanPath(folder);
    return cleanPath == cleanFolder || cleanPath.startsWith(cleanFolder + QLatin1Char('/'));
}

QString copy(const QString &source, const QString &destination)
{
    if (!QFileInfo::exists(source) && !QFileInfo(source).isSymLink())
        return translate("“%1” no longer exists").arg(QFileInfo(source).fileName());
    if (QFileInfo::exists(destination))
        return translate("“%1” already exists").arg(QFileInfo(destination).fileName());
    if (QFileInfo(source).isDir() && isInside(destination, source))
        return translate("A folder cannot be copied into itself");
    QString error;
    if (!copyTree(source, destination, &error)) {
        // Nothing half done.
        if (QFileInfo(destination).isDir())
            QDir(destination).removeRecursively();
        else
            QFile::remove(destination);
        return error;
    }
    return {};
}

QString move(const QString &source, const QString &destination)
{
    if (!QFileInfo::exists(source) && !QFileInfo(source).isSymLink())
        return translate("“%1” no longer exists").arg(QFileInfo(source).fileName());
    if (QFileInfo::exists(destination))
        return translate("“%1” already exists").arg(QFileInfo(destination).fileName());
    if (isInside(destination, source))
        return translate("A folder cannot be moved into itself");
    if (QDir().rename(source, destination))
        return {};
    // Another file system: copy, then delete the original.
    if (const QString error = copy(source, destination); !error.isEmpty())
        return error;
    const bool removed = QFileInfo(source).isDir() && !QFileInfo(source).isSymLink() ? QDir(source).removeRecursively()
                                                                                     : QFile::remove(source);
    return removed ? QString()
                   : translate("“%1” was copied but could not be deleted").arg(QFileInfo(source).fileName());
}

QString copyName(const QString &folder, const QString &name)
{
    const auto [base, extension] = splitName(name);
    const QDir dir(folder);
    for (int i = 1;; ++i) {
        const QString candidate = base + QStringLiteral("_copy") + (i > 1 ? QString::number(i) : QString()) + extension;
        if (!QFileInfo::exists(dir.filePath(candidate)))
            return candidate;
    }
}

QString freeName(const QString &folder, const QString &name)
{
    const QDir dir(folder);
    if (!QFileInfo::exists(dir.filePath(name)))
        return name;
    const auto [base, extension] = splitName(name);
    for (int i = 2;; ++i) {
        const QString candidate = QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(extension);
        if (!QFileInfo::exists(dir.filePath(candidate)))
            return candidate;
    }
}

} // namespace FileOperations
