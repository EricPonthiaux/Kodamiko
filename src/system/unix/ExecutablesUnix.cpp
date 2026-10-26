// SPDX-License-Identifier: GPL-3.0-only

#include "system/Executables.h"

namespace Executables {

QString fileName(const QString &path)
{
    return path;
}

QString virtualEnvironmentPython(const QString &folder)
{
    return folder + QStringLiteral("/bin/python");
}

QStringList pythonNames()
{
    // Distributions install python3; some only a versioned name.
    return {QStringLiteral("python3"),    QStringLiteral("python"),     QStringLiteral("python3.15"),
            QStringLiteral("python3.14"), QStringLiteral("python3.13"), QStringLiteral("python3.12"),
            QStringLiteral("python3.11"), QStringLiteral("python3.10"), QStringLiteral("python3.9"),
            QStringLiteral("python3.8")};
}

} // namespace Executables
