#pragma once

#include <QIcon>

// File tree icons, drawn rather than loaded: a page with a colored badge
// naming the type ("C+", "H", "TS", "{}"…), in the manner of IntelliJ's new
// UI; a folder; a picture for images. Two variants, for dark and light
// backgrounds.
namespace FileIcons {

// Kind of icon for a file name: "cpp", "header", "json", "image"… or
// "text" when the type is not recognized.
QString kindOf(const QString &fileName);

QIcon file(const QString &fileName, bool dark);
QIcon folder(bool dark);

} // namespace FileIcons
