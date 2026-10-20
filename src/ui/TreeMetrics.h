// SPDX-License-Identifier: GPL-3.0-only

#pragma once

class QFont;
class QTreeView;

// Proportions of the side trees, for their font (Preferences::treeFont:
// by default the UI font at the editor's size, so that navigation reads at
// the same distance as the code):
//  - row: 1.6 × the text height, the spacing of typical file lists;
//  - icon: the text height;
//  - indentation: 1.25 × the text height, to follow depth at a glance.
// A tree drawn by its own delegate keeps it: that delegate sizes its rows
// (with treeRowHeight, or its own layout).
void applyTreeMetrics(QTreeView *tree, const QFont &editorFont);

// Row height of a tree whose text is in `font`.
int treeRowHeight(const QFont &font);
