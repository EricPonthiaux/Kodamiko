// SPDX-License-Identifier: GPL-3.0-only

#include "toolchains/SystemJdks.h"

namespace SystemJdks {

QStringList parentFolders()
{
    QStringList parents;
    for (const char *variable : {"ProgramFiles", "ProgramW6432"}) {
        const QString programFiles = qEnvironmentVariable(variable);
        if (programFiles.isEmpty())
            continue;
        for (const char *vendor :
             {"Java", "Eclipse Adoptium", "Microsoft", "Zulu", "Amazon Corretto", "BellSoft", "Semeru", "OpenJDK"})
            parents.append(programFiles + QLatin1Char('/') + QLatin1String(vendor));
    }
    return parents;
}

} // namespace SystemJdks
