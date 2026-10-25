// SPDX-License-Identifier: GPL-3.0-only

#include "SystemFixtures.h"

namespace SystemFixtures {

QString programOfEveryMachine()
{
    return QStringLiteral("sh");
}

QString printingThenExiting(const QString &text, int exitCode)
{
    return QStringLiteral("echo %1; exit %2").arg(text).arg(exitCode);
}

QString succeedingIfFileExists(const QString &fileName)
{
    return QStringLiteral("test -f %1").arg(fileName);
}

} // namespace SystemFixtures
