// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <QUrl>

// What the file system adds to Duet's rules on names, and file paths in
// URLs. One implementation per system: FilePathsWindows.cpp,
// FilePathsUnix.cpp.
namespace FilePaths {

// Whether the system accepts `name`, one component of a path, beyond
// Duet's own rules (not empty, no "/", no control character): Windows
// refuses <>:"|?*\ and a final dot or space.
bool systemAcceptsName(const QString &name);

// The path of `filePath` in a URL, which starts with a slash: Windows'
// "C:/a.html" is "/C:/a.html" there.
QString toUrlPath(const QString &filePath);

// The file path of the URL path `urlPath`, cleaned: toUrlPath reversed.
QString fromUrlPath(const QString &urlPath);

// The file path of the file URL `url`, its drive as Qt writes it: language
// servers send Windows' "C:/a.ts" as "file:///c%3A/a.ts", and a path
// compared with Duet's must say "C:".
QString fromFileUrl(const QUrl &url);

} // namespace FilePaths
