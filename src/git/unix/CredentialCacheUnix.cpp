// SPDX-License-Identifier: GPL-3.0-only

#include "git/CredentialCache.h"

#include <chrono>

namespace CredentialCache {
namespace {

constexpr std::chrono::seconds cacheLifetime = std::chrono::days(7);

// git runs the helper through the shell.
QString shellQuoted(QString text)
{
    text.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + text + QLatin1Char('\'');
}

} // namespace

QString helper(const QString &workDir)
{
    return QStringLiteral("cache --timeout=%1 --socket=%2")
        .arg(cacheLifetime.count())
        .arg(shellQuoted(socketPath(workDir)));
}

} // namespace CredentialCache
