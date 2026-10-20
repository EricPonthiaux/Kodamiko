#include "ChangeTree.h"

#include "FileChangeDelegate.h"

#include <QTreeWidgetItem>

#include <algorithm>
#include <utility>
#include <vector>

namespace {

// A folder while the tree is built. std::vector: it may hold its own type.
struct Folder {
    QString name;
    std::vector<Folder> folders;
    std::vector<std::pair<QString, int>> files; // name, index in the paths
};

Folder &subfolder(Folder &folder, const QString &name)
{
    for (Folder &child : folder.folders) {
        if (child.name == name)
            return child;
    }
    return folder.folders.emplace_back(Folder{name, {}, {}});
}

Folder treeOf(const QStringList &paths)
{
    Folder root;
    for (int i = 0; i < paths.size(); ++i) {
        const QStringList parts = paths[i].split(QLatin1Char('/'), Qt::SkipEmptyParts);
        Folder *folder = &root;
        for (qsizetype p = 0; p + 1 < parts.size(); ++p)
            folder = &subfolder(*folder, parts[p]);
        folder->files.emplace_back(parts.value(parts.size() - 1), i);
    }
    return root;
}

bool isBefore(const QString &a, const QString &b)
{
    return QString::compare(a, b, Qt::CaseInsensitive) < 0;
}

void addFolder(QTreeWidgetItem *parent, Folder &folder, const QString &path, QList<QTreeWidgetItem *> &fileRows)
{
    std::ranges::sort(folder.folders, isBefore, &Folder::name);
    for (Folder &child : folder.folders) {
        QString label = child.name;
        Folder *shown = &child;
        // "src" holding only "changes": one row, "src/changes".
        while (shown->files.empty() && shown->folders.size() == 1) {
            shown = &shown->folders.front();
            label += QLatin1Char('/') + shown->name;
        }
        const QString childPath = path.isEmpty() ? label : path + QLatin1Char('/') + label;
        auto *row = new QTreeWidgetItem(parent);
        FileChangeDelegate::setFolder(row, label, childPath);
        addFolder(row, *shown, childPath, fileRows);
        row->setExpanded(true);
    }
    std::ranges::sort(folder.files, isBefore, &std::pair<QString, int>::first);
    for (const auto &[name, index] : folder.files)
        fileRows[index] = new QTreeWidgetItem(parent);
}

} // namespace

namespace ChangeTree {

QList<QTreeWidgetItem *> addFiles(QTreeWidgetItem *parent, const QStringList &paths)
{
    Folder root = treeOf(paths);
    QList<QTreeWidgetItem *> fileRows(paths.size(), nullptr);
    addFolder(parent, root, {}, fileRows);
    return fileRows;
}

bool comesBefore(const QString &a, const QString &b)
{
    const QStringList left = a.split(QLatin1Char('/'));
    const QStringList right = b.split(QLatin1Char('/'));
    for (qsizetype i = 0; i < std::min(left.size(), right.size()); ++i) {
        if (left[i] == right[i])
            continue;
        const bool isLeftAFolder = i + 1 < left.size();
        const bool isRightAFolder = i + 1 < right.size();
        if (isLeftAFolder != isRightAFolder)
            return isLeftAFolder;
        return isBefore(left[i], right[i]);
    }
    return left.size() < right.size();
}

QList<QTreeWidgetItem *> filesUnder(QTreeWidgetItem *item)
{
    if (item->data(0, FileChangeDelegate::pathRole).isValid())
        return {item};
    QList<QTreeWidgetItem *> files;
    for (int i = 0; i < item->childCount(); ++i)
        files += filesUnder(item->child(i));
    return files;
}

} // namespace ChangeTree
