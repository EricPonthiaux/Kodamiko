// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QAbstractItemModel>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

#include <functional>
#include <memory>
#include <vector>

class IgnoreRules;

// Files of the working folder for the file tree, as IntelliJ's Project
// view lays them out: the folder itself as the only top-level row, folders
// before files, names sorted without regard to case, and ".git" left out.
//
// - Compact folders: a chain of folders that each contain only one folder
//   is a single row, "src/main/java".
// - File nesting: generated or companion files are children of their main
//   file, Foo.h under Foo.cpp, package-lock.json under package.json,
//   x.js.map under x.js (see nestingParent).
// - Files ignored by git can be left out.
//
// Folders are read when first expanded (or when index(path) needs them),
// then watched: changes on disk update the rows in place, so the expanded
// folders and the selection stay.
class ProjectTreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    struct Options {
        bool compactFolders = true;
        bool nestFiles = true;
        bool showIgnored = true;
        bool operator==(const Options &) const = default;
    };

    explicit ProjectTreeModel(QObject *parent = nullptr);
    ~ProjectTreeModel() override;

    void setRootPath(const QString &path);

    QString rootPath() const { return rootPath_; }

    // Rebuilds the tree (a model reset).
    void setOptions(const Options &options);

    Options options() const { return options_; }

    // Icons for a dark or light background; the view must be repainted.
    void setDarkIcons(bool dark);

    // Row of `path`, reading the folders leading to it. A folder inside a
    // compact chain gives the chain's row, a nested file its row under its
    // main file. Invalid outside the tree (or for an ignored file hidden).
    QModelIndex index(const QString &path);
    using QAbstractItemModel::index;
    QString filePath(const QModelIndex &index) const;
    bool isDir(const QModelIndex &index) const;
    // Rereads every loaded folder now instead of waiting for the watcher.
    void refresh();

    // File name that `fileName` nests under, among `siblings` (the names in
    // its folder); empty if none. Follows the chain to the top: x.js.map
    // goes under x.ts when both x.js and x.ts exist.
    static QString nestingParent(const QString &fileName, const QStringList &siblings);

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    bool hasChildren(const QModelIndex &parent = {}) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDragActions() const override;
    Qt::DropActions supportedDropActions() const override;

signals:
    // The folder's contents are known (first reading).
    void directoryLoaded(const QString &path);

private:
    struct Node;
    struct Entry;

    Node *nodeOf(const QModelIndex &index) const;
    QModelIndex indexOf(Node *node) const;

    // Where stepToward() leads: the row of the path, or the folder to read
    // next; neither when the path is not in the tree.
    struct PathStep {
        Node *found = nullptr;
        Node *next = nullptr;
    };

    // Entries of a folder as rows: compact chains, nested files.
    std::vector<Entry> list(const QString &folder) const;
    Entry makeEntry(const QFileInfo &info, bool compact) const;
    // Extends a folder's row down its chain of single subfolders.
    void followChain(Entry &entry) const;
    // Visible entries of a folder, folders first.
    QFileInfoList visibleEntries(const QString &folder) const;
    void load(Node *node);
    // Brings the children of `node` in line with `entries`, keeping the
    // rows that still exist.
    void update(Node *node, std::vector<Entry> entries);
    void removeGoneRows(Node *node, const std::vector<Entry> &entries);
    void insertNewRows(Node *node, std::vector<Entry> entries);
    void setChildren(Node *node, std::vector<Entry> entries);
    std::unique_ptr<Node> makeNode(Entry entry, Node *parent);
    void watch(Node *node);
    void onDirectoryChanged(const QString &path);
    void reloadPending();
    QStringList foldersToUpdate(const QStringList &changed) const;
    // Walks down the loaded folders toward `path`: the first row for which
    // `isTarget` holds, or null.
    Node *findRow(const QString &path, const std::function<bool(const Node &)> &isTarget) const;
    static PathStep stepToward(const Node &node, const QString &target);
    // `row` itself, or its nested file, when it shows `target`; else null.
    static Node *rowShowing(Node *row, const QString &target);
    void reset();

    QString rootPath_;
    Options options_;
    bool darkIcons_ = true;
    std::unique_ptr<Node> invisibleRoot_;
    std::unique_ptr<IgnoreRules> ignoreRules_;
    QFileSystemWatcher watcher_;
    QStringList changedFolders_;
    QTimer reloadTimer_; // coalesces bursts of changes
};
