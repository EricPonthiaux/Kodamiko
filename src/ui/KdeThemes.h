#pragma once

#include "Theme.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

// The editor themes of KDE's KSyntaxHighlighting, those Kate and KDevelop
// offer (Breeze, Solarized, Dracula, Nord…), embedded from
// third_party/ksyntaxhighlighting-themes. A .theme file gives the editor's
// colors and its text styles (Keyword, Function, String…): the syntax
// categories take the matching style, and the interface around the editor
// (panels, borders, accent) is derived from the editor's colors.
namespace KdeThemes {

// Every embedded theme, sorted by title; read on first use. Their names
// are "kde-" and the file's name: "kde-breeze-dark".
const QList<Theme> &all();

// The theme a .theme file describes, called `name`; nullopt if it is not
// one.
std::optional<Theme> fromJson(const QByteArray &json, const QString &name);

} // namespace KdeThemes
