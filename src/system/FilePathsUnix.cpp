// SPDX-License-Identifier: GPL-3.0-only

#include "FilePaths.h"

#include <QDir>

namespace FilePaths {

bool systemAcceptsName(const QString &name)
{
    Q_UNUSED(name)
    return true;
}

QString toUrlPath(const QString &filePath)
{
    return filePath;
}

QString fromUrlPath(const QString &urlPath)
{
    return QDir::cleanPath(urlPath);
}

QString fromFileUrl(const QUrl &url)
{
    return url.toLocalFile();
}

} // namespace FilePaths
