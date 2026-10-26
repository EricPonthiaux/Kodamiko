// SPDX-License-Identifier: GPL-3.0-only

#include "support/SystemFixtures.h"

namespace SystemFixtures {

QString programOfEveryMachine()
{
    return QStringLiteral("cmd");
}

QString printingThenExiting(const QString &text, int exitCode)
{
    // cmd.exe: "&" chains; a space before it would be printed.
    return QStringLiteral("echo %1& exit %2").arg(text).arg(exitCode);
}

QString succeedingIfFileExists(const QString &fileName)
{
    // cmd.exe has no test command.
    return QStringLiteral("if exist %1 (exit 0) else (exit 1)").arg(fileName);
}

} // namespace SystemFixtures
