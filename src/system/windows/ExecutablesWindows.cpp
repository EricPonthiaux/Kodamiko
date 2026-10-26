// SPDX-License-Identifier: GPL-3.0-only

#include "system/Executables.h"

namespace Executables {

QString fileName(const QString &path)
{
    return path + QStringLiteral(".exe");
}

QString virtualEnvironmentPython(const QString &folder)
{
    return folder + QStringLiteral("/Scripts/python.exe");
}

QStringList pythonNames()
{
    // python.org's installer and the Store's install python.exe only.
    return {QStringLiteral("python"), QStringLiteral("python3")};
}

} // namespace Executables
