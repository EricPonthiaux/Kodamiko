// SPDX-License-Identifier: GPL-3.0-only

#include "CredentialCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace CredentialCache {
namespace {

constexpr int socketNameLength = 16;

QString repositoryRoot(const QString &workDir)
{
    const QFileInfo start(workDir);
    QString startPath = start.canonicalFilePath().isEmpty() ? start.absoluteFilePath() : start.canonicalFilePath();
    QDir dir(startPath);
    do {
        if (QFileInfo::exists(dir.filePath(QStringLiteral(".git"))))
            return dir.path();
    } while (dir.cdUp());
    return startPath;
}

} // namespace

QString socketPath(const QString &workDir)
{
    const QByteArray hash =
        QCryptographicHash::hash(repositoryRoot(workDir).toUtf8(), QCryptographicHash::Sha256).toHex();

    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QLatin1String("/duet/credentials");
    return directory + QLatin1Char('/') + QString::fromLatin1(hash.left(socketNameLength));
}

} // namespace CredentialCache
