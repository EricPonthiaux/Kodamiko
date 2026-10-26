// SPDX-License-Identifier: GPL-3.0-only

#include "git/CredentialCache.h"

namespace CredentialCache {

QString helper(const QString &workDir)
{
    // git's credential-cache needs Unix sockets: Git for Windows' own
    // credential manager keeps the credentials instead.
    Q_UNUSED(workDir)
    return {};
}

} // namespace CredentialCache
