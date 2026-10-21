// SPDX-License-Identifier: GPL-3.0-only

#include "CredentialCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <chrono>

namespace CredentialCache {
namespace {

constexpr std::chrono::seconds cacheLifetime = std::chrono::days(7);

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

QString shellQuoted(QString text)
{
    text.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + text + QLatin1Char('\'');
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

QString helper(const QString &workDir)
{
#ifdef Q_OS_WIN
    Q_UNUSED(workDir)
    return {};
#else
    return QStringLiteral("cache --timeout=%1 --socket=%2")
        .arg(cacheLifetime.count())
        .arg(shellQuoted(socketPath(workDir)));
#endif
}

} // namespace CredentialCache
