// SPDX-License-Identifier: GPL-3.0-only

#include "Executables.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace Executables {

QString besideTheApplication(const QString &name)
{
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(fileName(name));
    return QFileInfo::exists(path) ? path : QString();
}

} // namespace Executables
