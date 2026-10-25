// SPDX-License-Identifier: GPL-3.0-only

#include "FilePaths.h"

#include <QDir>

#include <algorithm>

namespace FilePaths {

bool systemAcceptsName(const QString &name)
{
    static const QString reserved = QStringLiteral("<>:\"|?*\\");
    const bool hasReservedCharacter = std::ranges::any_of(name, [](QChar c) { return reserved.contains(c); });
    const bool endsWithDotOrSpace = name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '));
    return !hasReservedCharacter && !endsWithDotOrSpace;
}

QString toUrlPath(const QString &filePath)
{
    return filePath.startsWith(u'/') ? filePath : u'/' + filePath;
}

QString fromUrlPath(const QString &urlPath)
{
    const bool hasDrive =
        urlPath.size() > 2 && urlPath.at(0) == u'/' && urlPath.at(1).isLetter() && urlPath.at(2) == u':';
    return QDir::cleanPath(hasDrive ? urlPath.mid(1) : urlPath);
}

QString fromFileUrl(const QUrl &url)
{
    QString path = url.toLocalFile();
    const bool hasDrive = path.size() > 1 && path.at(0).isLetter() && path.at(1) == u':';
    if (hasDrive)
        path[0] = path.at(0).toUpper();
    return path;
}

} // namespace FilePaths
