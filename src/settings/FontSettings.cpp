// SPDX-License-Identifier: GPL-3.0-only

#include "FontSettings.h"

#include "TreeZoom.h"
#include "ui/ThemeStyle.h"

#include <QTreeView>
#include <QWidget>

namespace FontSettings {

void apply(const Preferences &preferences, const Preferences &previous, const QWidget *window)
{
    if (preferences.interfaceFont() != previous.interfaceFont())
        ThemeStyle::applyFont(preferences.interfaceFont());
    if (preferences.treeFont() != previous.treeFont())
        applyTreeFont(preferences.treeFont(), window);
}

void applyTreeFont(const QFont &font, const QWidget *window)
{
    const QList<QTreeView *> trees = window->findChildren<QTreeView *>();
    for (QTreeView *tree : trees)
        TreeZoom::setFont(tree, font);
}

} // namespace FontSettings
