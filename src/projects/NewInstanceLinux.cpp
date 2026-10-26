// SPDX-License-Identifier: GPL-3.0-only

#include "NewInstance.h"

#include <QCoreApplication>

// In an AppImage, the executable lives in a mount that goes away with this
// process: the new one starts from the image itself.
NewInstance::Command NewInstance::command(const QString &folder)
{
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QString program = appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
    return {.program = program, .arguments = {folder}};
}
