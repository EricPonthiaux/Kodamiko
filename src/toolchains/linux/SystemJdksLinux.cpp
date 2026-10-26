// SPDX-License-Identifier: GPL-3.0-only

#include "toolchains/SystemJdks.h"

namespace SystemJdks {

QStringList parentFolders()
{
    return {QStringLiteral("/usr/lib/jvm"), QStringLiteral("/usr/lib64/jvm"), QStringLiteral("/usr/java"),
            QStringLiteral("/opt/java"), QStringLiteral("/opt")};
}

} // namespace SystemJdks
