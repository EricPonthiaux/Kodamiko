// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QStringList>

class QTreeWidgetItem;

// Changed files in their folders, in a QTreeWidget: a folder row per
// folder (see FileChangeDelegate::setFolder), folders first, then files,
// each sorted by name. A folder whose only content is one folder merges
// with it ("src/changes"), as in VS Code.
namespace ChangeTree {

// Adds a row for each of `paths` ("src/a.cpp", "/" separators) under
// `parent`, with the folder rows they need, expanded. Returns the file rows
// in the order of `paths`, for the caller to fill; the folder rows are
// filled. The rows belong to `parent`.
QList<QTreeWidgetItem *> addFiles(QTreeWidgetItem *parent, const QStringList &paths);

// Whether the file at `a` comes before the one at `b` in the tree: at their
// first different part, a folder before a file, then by name. For lists to
// follow the tree's order.
bool comesBefore(const QString &a, const QString &b);

// The file rows at or under `item`: `item` itself if it is one, otherwise
// those of its folders, in the tree's order.
QList<QTreeWidgetItem *> filesUnder(QTreeWidgetItem *item);

} // namespace ChangeTree
