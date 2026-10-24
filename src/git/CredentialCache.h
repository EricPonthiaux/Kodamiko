// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

// Remembers the credentials of each repository for a week, so that the user
// types them once per project rather than at every fetch or push.
//
// git's own credential-cache keeps them in memory (a daemon started by git,
// lost on reboot), one cache per repository: its socket lives in the user's
// cache directory, outside the sources, under a name derived from the
// repository's path. Git::run adds this helper to every git it starts, after
// the user's own helpers, which keep working.
namespace CredentialCache {

// The credential.helper value for the repository containing `workDir` (the
// repository itself when `workDir` is not in one); empty where git has no
// credential cache (Windows). One implementation per system:
// CredentialCacheWindows.cpp, CredentialCacheUnix.cpp.
QString helper(const QString &workDir);

// The socket of that repository's cache daemon; "git credential-cache exit
// --socket=<it>" stops it and forgets the credentials.
QString socketPath(const QString &workDir);

} // namespace CredentialCache
