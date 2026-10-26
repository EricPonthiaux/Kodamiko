// SPDX-License-Identifier: GPL-3.0-only

#include "NewInstance.h"

#include <QCoreApplication>
#include <QDir>

// Through LaunchServices (open -n), which brings the new window forward,
// rather than the bundle's executable started directly. A build outside a
// bundle starts its executable.
NewInstance::Command NewInstance::command(const QString &folder)
{
    QDir bundle(QCoreApplication::applicationDirPath()); // Duet.app/Contents/MacOS
    bundle.cdUp();
    bundle.cdUp();
    if (!bundle.dirName().endsWith(QLatin1String(".app")))
        return {.program = QCoreApplication::applicationFilePath(), .arguments = {folder}};
    return {.program = QStringLiteral("/usr/bin/open"),
            .arguments = {QStringLiteral("-n"), QStringLiteral("-a"), bundle.absolutePath(), QStringLiteral("--args"),
                          folder}};
}
