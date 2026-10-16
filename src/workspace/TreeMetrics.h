#pragma once

class QFont;
class QTreeView;

// Proportions of the side trees, matched to the editor font so that
// navigation reads at the same distance as the code:
//  - font: the UI font (Inter if available, see main.cpp);
//  - text: same size as the editor (1:1 ratio, never below the system font);
//  - row: 1.6 × the text height, the spacing of typical file lists;
//  - icon: the text height;
//  - indentation: 1.25 × the text height, to follow depth at a glance.
// A tree drawn by its own delegate keeps it: that delegate sizes its rows
// (with treeRowHeight, or its own layout).
void applyTreeMetrics(QTreeView *tree, const QFont &editorFont);

// Row height of a tree whose text is in `font`.
int treeRowHeight(const QFont &font);
