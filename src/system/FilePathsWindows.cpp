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

} // namespace FilePaths
