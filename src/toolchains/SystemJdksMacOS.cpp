// SPDX-License-Identifier: GPL-3.0-only

#include "SystemJdks.h"

#include <QDir>

namespace SystemJdks {

QStringList parentFolders()
{
    // The JDK bundles, then Homebrew's formulas (Apple Silicon, Intel).
    return {QStringLiteral("/Library/Java/JavaVirtualMachines"),
            QDir::homePath() + QStringLiteral("/Library/Java/JavaVirtualMachines"), QStringLiteral("/opt/homebrew/opt"),
            QStringLiteral("/usr/local/opt")};
}

} // namespace SystemJdks
