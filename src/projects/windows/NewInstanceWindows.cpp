// SPDX-License-Identifier: GPL-3.0-only

#include "projects/NewInstance.h"

#include <QCoreApplication>

NewInstance::Command NewInstance::command(const QString &folder)
{
    return {.program = QCoreApplication::applicationFilePath(), .arguments = {folder}};
}
