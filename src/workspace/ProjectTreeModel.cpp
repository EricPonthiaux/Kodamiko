#include "ProjectTreeModel.h"

#include "FileIcons.h"
#include "git/IgnoreRules.h"

#include <QDir>
#include <QMimeData>
#include <QSet>
#include <QUrl>

#include <algorithm>

namespace {

// Beyond this many subfolders, folders are not compacted: finding out
// would mean reading each of them.
constexpr int maxCompactedFolders = 200;
constexpr int maxChainLength = 50;
constexpr int reloadDelayMs = 150;
// Guards against cycles while walking down toward a path.
constexpr int maxTreeDepth = 512;

// Main file suffix ← companion file suffix, for names that differ only by
// these suffixes ("Foo.cpp" ← "Foo.h"). Without a leading dot, whole names.
struct NestingRule {
    const char *parent;
    const char *child;
};

constexpr auto nestingRules = std::to_array<NestingRule>({
    {".cpp", ".h"},
    {".cpp", ".hpp"},
    {".cc", ".h"},
    {".cc", ".hh"},
    {".cxx", ".h"},
    {".cxx", ".hxx"},
    {".c", ".h"},
    {".ts", ".js"},
    {".ts", ".js.map"},
    {".ts", ".d.ts"},
    {".tsx", ".js"},
    {".tsx", ".js.map"},
    {".js", ".js.map"},
    {".js", ".min.js"},
    {".scss", ".css"},
    {".scss", ".css.map"},
    {".less", ".css"},
    {".css", ".css.map"},
    {".css", ".min.css"},
    {"package.json", "package-lock.json"},
    {"package.json", "yarn.lock"},
    {"package.json", "pnpm-lock.yaml"},
    {"Cargo.toml", "Cargo.lock"},
    {"pyproject.toml", "poetry.lock"},
    {"go.mod", "go.sum"},
    {"Gemfile", "Gemfile.lock"},
    {"composer.json", "composer.lock"},
});

// Direct main file of `name` among `siblings`; empty if none. The longest
// companion suffix is tried first: "x.d.ts" is a declaration, not a ".ts".
QString directParent(const QString &name, const QStringList &siblings)
{
    static const std::vector<NestingRule> rules = [] {
        std::vector<NestingRule> sorted(nestingRules.begin(), nestingRules.end());
        std::stable_sort(sorted.begin(), sorted.end(), [](const NestingRule &a, const NestingRule &b) {
            return qstrlen(a.child) > qstrlen(b.child);
        });
        return sorted;
    }();
    for (const NestingRule &rule : rules) {
        const QLatin1String child(rule.child);
        const QLatin1String parent(rule.parent);
        QString candidate;
        if (child.startsWith(QLatin1Char('.'))) {
            if (!name.endsWith(child) || name.size() == child.size())
                continue;
            candidate = name.chopped(child.size()) + parent;
        } else {
            if (name != child)
                continue;
            candidate = parent;
        }
        if (candidate != name && siblings.contains(candidate))
            return candidate;
    }
    return {};
}

bool lessThan(const QFileInfo &a, const QFileInfo &b)
{
    if (a.isDir() != b.isDir())
        return a.isDir();
    const int order = a.fileName().compare(b.fileName(), Qt::CaseInsensitive);
    return order != 0 ? order < 0 : a.fileName() < b.fileName();
}

QStringList fileNamesOf(const QFileInfoList &entries)
{
    QStringList names;
    for (const QFileInfo &entry : entries) {
        if (!entry.isDir())
            names.append(entry.fileName());
    }
    return names;
}

// Identity of a row across rereadings of its folder.
QString rowKey(const QString &path, bool isDir, const QStringList &chain)
{
    return path + (isDir ? QStringLiteral("\nd\n") : QStringLiteral("\nf\n")) + chain.join(QLatin1Char('\n'));
}

} // namespace

struct ProjectTreeModel::Entry {
    QString path;
    QString name;
    bool isDir = false;
    QStringList chain; // folders shown by the row before `path`, outermost first
    std::vector<Entry> nested;
};

struct ProjectTreeModel::Node {
    QString path;
    QString name;
    bool isDir = false;
    bool loaded = false; // folders: children read; files: always
    QStringList chain;
    Node *parent = nullptr;
    int row = 0;
    std::vector<std::unique_ptr<Node>> children;

    void renumber()
    {
        for (size_t i = 0; i < children.size(); ++i)
            children[i]->row = static_cast<int>(i);
    }
};

ProjectTreeModel::ProjectTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
    , invisibleRoot_(std::make_unique<Node>())
{
    reloadTimer_.setSingleShot(true);
    reloadTimer_.setInterval(reloadDelayMs);
    connect(&reloadTimer_, &QTimer::timeout, this, &ProjectTreeModel::reloadPending);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, &ProjectTreeModel::onDirectoryChanged);
}

ProjectTreeModel::~ProjectTreeModel() = default;

void ProjectTreeModel::setRootPath(const QString &path)
{
    rootPath_ = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    reset();
}

void ProjectTreeModel::setOptions(const Options &options)
{
    if (options == options_)
        return;
    options_ = options;
    reset();
}

void ProjectTreeModel::setDarkIcons(bool dark)
{
    if (dark == darkIcons_)
        return;
    darkIcons_ = dark;
}

void ProjectTreeModel::reset()
{
    beginResetModel();
    if (!watcher_.directories().isEmpty())
        watcher_.removePaths(watcher_.directories());
    changedFolders_.clear();
    invisibleRoot_ = std::make_unique<Node>();
    invisibleRoot_->isDir = true;
    invisibleRoot_->loaded = true;
    ignoreRules_.reset();
    if (!rootPath_.isEmpty()) {
        ignoreRules_ = std::make_unique<IgnoreRules>(rootPath_);
        auto root = std::make_unique<Node>();
        root->path = rootPath_;
        root->name = QFileInfo(rootPath_).fileName();
        if (root->name.isEmpty())
            root->name = QDir::toNativeSeparators(rootPath_); // the filesystem root
        root->isDir = true;
        root->parent = invisibleRoot_.get();
        invisibleRoot_->children.push_back(std::move(root));
    }
    endResetModel();
}

QFileInfoList ProjectTreeModel::visibleEntries(const QString &folder) const
{
    QFileInfoList result;
    const QFileInfoList entries =
        QDir(folder).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo &entry : entries) {
        if (entry.fileName() == QLatin1String(".git"))
            continue;
        if (!options_.showIgnored && ignoreRules_ && ignoreRules_->isIgnored(entry.absoluteFilePath(), entry.isDir()))
            continue;
        result.append(entry);
    }
    std::sort(result.begin(), result.end(), lessThan);
    return result;
}

std::vector<ProjectTreeModel::Entry> ProjectTreeModel::list(const QString &folder) const
{
    const QFileInfoList entries = visibleEntries(folder);
    const auto folders = std::count_if(entries.begin(), entries.end(), [](const QFileInfo &e) { return e.isDir(); });
    const bool compact = options_.compactFolders && folders <= maxCompactedFolders;
    const QStringList fileNames = fileNamesOf(entries);

    std::vector<Entry> result;
    QHash<QString, size_t> fileRows;     // file name → index in result
    QList<QPair<QString, Entry>> nested; // main file name, companion
    for (const QFileInfo &info : entries) {
        Entry entry = makeEntry(info, compact);
        const bool mayNest = !entry.isDir && options_.nestFiles;
        const QString parent = mayNest ? nestingParent(entry.name, fileNames) : QString();
        if (!parent.isEmpty()) {
            nested.append({parent, std::move(entry)});
            continue;
        }
        if (!entry.isDir)
            fileRows.insert(entry.name, result.size());
        result.push_back(std::move(entry));
    }
    for (auto &[parent, entry] : nested) {
        if (const auto row = fileRows.constFind(parent); row != fileRows.cend())
            result[*row].nested.push_back(std::move(entry));
    }
    return result;
}

ProjectTreeModel::Entry ProjectTreeModel::makeEntry(const QFileInfo &info, bool compact) const
{
    Entry entry;
    entry.path = QDir::cleanPath(info.absoluteFilePath());
    entry.name = info.fileName();
    entry.isDir = info.isDir();
    if (entry.isDir && compact && !info.isSymLink())
        followChain(entry);
    return entry;
}

void ProjectTreeModel::followChain(Entry &entry) const
{
    for (int depth = 0; depth < maxChainLength; ++depth) {
        const QFileInfoList inside = visibleEntries(entry.path);
        if (inside.size() != 1 || !inside.first().isDir() || inside.first().isSymLink())
            return;
        entry.chain.append(entry.path);
        entry.path = QDir::cleanPath(inside.first().absoluteFilePath());
        entry.name += QLatin1Char('/') + inside.first().fileName();
    }
}

QString ProjectTreeModel::nestingParent(const QString &fileName, const QStringList &siblings)
{
    QString top;
    QString current = fileName;
    for (int depth = 0; depth < 8; ++depth) {
        const QString parent = directParent(current, siblings);
        if (parent.isEmpty() || parent == fileName)
            break;
        top = current = parent;
    }
    return top;
}

std::unique_ptr<ProjectTreeModel::Node> ProjectTreeModel::makeNode(Entry entry, Node *parent)
{
    auto node = std::make_unique<Node>();
    node->path = std::move(entry.path);
    node->name = std::move(entry.name);
    node->isDir = entry.isDir;
    node->chain = std::move(entry.chain);
    node->parent = parent;
    node->loaded = !node->isDir;
    for (Entry &child : entry.nested)
        node->children.push_back(makeNode(std::move(child), node.get()));
    node->renumber();
    return node;
}

void ProjectTreeModel::setChildren(Node *node, std::vector<Entry> entries)
{
    for (Entry &entry : entries)
        node->children.push_back(makeNode(std::move(entry), node));
    node->renumber();
}

void ProjectTreeModel::load(Node *node)
{
    if (!node || !node->isDir || node->loaded)
        return;
    std::vector<Entry> entries = list(node->path);
    if (entries.empty()) {
        node->loaded = true;
    } else {
        beginInsertRows(indexOf(node), 0, static_cast<int>(entries.size()) - 1);
        node->loaded = true;
        setChildren(node, std::move(entries));
        endInsertRows();
    }
    watch(node);
    emit directoryLoaded(node->path);
}

void ProjectTreeModel::watch(Node *node)
{
    QStringList paths = node->chain;
    paths.append(node->path);
    // Intermediate folders of the children's chains: a file added there
    // splits the chain.
    for (const auto &child : node->children)
        paths += child->chain;
    const QStringList directories = watcher_.directories();
    const QSet<QString> watched(directories.cbegin(), directories.cend());
    paths.removeIf([&watched](const QString &path) { return watched.contains(path); });
    if (!paths.isEmpty())
        watcher_.addPaths(paths);
}

// Linear in the folder's size, with one notification per run of rows: a
// folder of generated files changes thousands of rows at once.
void ProjectTreeModel::update(Node *node, std::vector<Entry> entries)
{
    removeGoneRows(node, entries);
    insertNewRows(node, std::move(entries));
}

// Rows that are gone, from the end, a run at a time.
void ProjectTreeModel::removeGoneRows(Node *node, const std::vector<Entry> &entries)
{
    QSet<QString> present;
    present.reserve(static_cast<qsizetype>(entries.size()));
    for (const Entry &entry : entries)
        present.insert(rowKey(entry.path, entry.isDir, entry.chain));

    const QModelIndex parent = indexOf(node);
    auto &children = node->children;
    const auto isPresent = [&](int row) {
        const Node &child = *children[static_cast<size_t>(row)];
        return present.contains(rowKey(child.path, child.isDir, child.chain));
    };
    for (int row = static_cast<int>(children.size()) - 1; row >= 0;) {
        if (isPresent(row)) {
            --row;
            continue;
        }
        int first = row;
        while (first > 0 && !isPresent(first - 1))
            --first;
        beginRemoveRows(parent, first, row);
        children.erase(children.begin() + first, children.begin() + row + 1);
        node->renumber();
        endRemoveRows();
        row = first - 1;
    }
}

// New rows, at their place, a run at a time; the remaining ones are in the
// same order.
void ProjectTreeModel::insertNewRows(Node *node, std::vector<Entry> entries)
{
    const QModelIndex parent = indexOf(node);
    auto &children = node->children;
    // Whether the existing row `childRow` is the entry `entryRow`.
    const auto isSameRow = [&](size_t childRow, size_t entryRow) {
        if (childRow >= children.size())
            return false;
        const Node &existing = *children[childRow];
        const Entry &entry = entries[entryRow];
        return existing.path == entry.path && existing.isDir == entry.isDir && existing.chain == entry.chain;
    };
    for (size_t i = 0; i < entries.size();) {
        if (isSameRow(i, i)) {
            Node *child = children[i].get();
            if (!child->isDir)
                update(child, std::move(entries[i].nested)); // nested files
            ++i;
            continue;
        }
        size_t end = i + 1;
        while (end < entries.size() && !isSameRow(i, end))
            ++end;
        beginInsertRows(parent, static_cast<int>(i), static_cast<int>(end - 1));
        std::vector<std::unique_ptr<Node>> added;
        added.reserve(end - i);
        for (size_t j = i; j < end; ++j)
            added.push_back(makeNode(std::move(entries[j]), node));
        children.insert(children.begin() + static_cast<std::ptrdiff_t>(i), std::make_move_iterator(added.begin()),
                        std::make_move_iterator(added.end()));
        node->renumber();
        endInsertRows();
        i = end;
    }
}

void ProjectTreeModel::onDirectoryChanged(const QString &path)
{
    if (!changedFolders_.contains(path))
        changedFolders_.append(path);
    reloadTimer_.start();
}

void ProjectTreeModel::refresh()
{
    std::function<void(Node *)> collect = [&](Node *node) {
        if (node->isDir && node->loaded) {
            if (!changedFolders_.contains(node->path))
                changedFolders_.append(node->path);
            for (const auto &child : node->children)
                collect(child.get());
        }
    };
    for (const auto &root : invisibleRoot_->children)
        collect(root.get());
    reloadPending();
}

void ProjectTreeModel::reloadPending()
{
    reloadTimer_.stop();
    const QStringList changed = std::exchange(changedFolders_, {});
    for (const QString &path : foldersToUpdate(changed)) {
        Node *node = findRow(path, [&path](const Node &row) { return row.path == path; });
        if (!node || !node->loaded)
            continue;
        if (!QFileInfo(node->path).isDir())
            continue; // removed: its parent's update takes care of it
        update(node, list(node->path));
        watch(node);
    }
}

// A change in a folder alters its own rows, and may alter its row in its
// parent (a compact chain that splits or grows). Outer folders come first:
// updating them may replace the inner ones.
QStringList ProjectTreeModel::foldersToUpdate(const QStringList &changed) const
{
    QList<Node *> toUpdate;
    const auto add = [&toUpdate](Node *node) {
        if (!toUpdate.contains(node))
            toUpdate.append(node);
    };
    for (const QString &folder : changed) {
        Node *row =
            findRow(folder, [&folder](const Node &node) { return node.path == folder || node.chain.contains(folder); });
        if (!row)
            continue;
        const bool isChangedFolder = row->isDir && row->loaded && row->path == folder;
        if (isChangedFolder)
            add(row);
        const bool hasParentRow = row->parent && row->parent != invisibleRoot_.get();
        if (hasParentRow)
            add(row->parent);
    }
    std::sort(toUpdate.begin(), toUpdate.end(), [](Node *a, Node *b) { return a->path.size() < b->path.size(); });
    QStringList paths;
    for (const Node *node : std::as_const(toUpdate))
        paths.append(node->path);
    return paths;
}

ProjectTreeModel::Node *ProjectTreeModel::findRow(const QString &path,
                                                  const std::function<bool(const Node &)> &isTarget) const
{
    const Node *node = invisibleRoot_.get();
    while (node) {
        const Node *next = nullptr;
        for (const auto &child : node->children) {
            if (isTarget(*child))
                return child.get();
            const bool leadsToPath = child->isDir && child->loaded && path.startsWith(child->path + QLatin1Char('/'));
            if (leadsToPath) {
                next = child.get();
                break;
            }
        }
        node = next;
    }
    return nullptr;
}

QModelIndex ProjectTreeModel::index(const QString &path)
{
    const QString target = QDir::cleanPath(path);
    if (invisibleRoot_->children.empty())
        return {};
    Node *node = invisibleRoot_->children.front().get();
    if (target == node->path)
        return indexOf(node);
    const bool isUnderRoot = target.startsWith(node->path + QLatin1Char('/')) || node->path == QLatin1String("/");
    if (!isUnderRoot)
        return {};
    for (int depth = 0; depth < maxTreeDepth && node; ++depth) {
        load(node);
        const PathStep step = stepToward(*node, target);
        if (step.found)
            return indexOf(step.found);
        node = step.next;
    }
    return {};
}

ProjectTreeModel::PathStep ProjectTreeModel::stepToward(const Node &node, const QString &target)
{
    for (const auto &child : node.children) {
        if (Node *row = rowShowing(child.get(), target))
            return {.found = row, .next = nullptr};
        if (child->isDir && target.startsWith(child->path + QLatin1Char('/')))
            return {.found = nullptr, .next = child.get()};
    }
    return {};
}

ProjectTreeModel::Node *ProjectTreeModel::rowShowing(Node *row, const QString &target)
{
    if (row->path == target || row->chain.contains(target))
        return row;
    if (row->isDir)
        return nullptr;
    const auto companion =
        std::ranges::find_if(row->children, [&target](const auto &file) { return file->path == target; });
    return companion != row->children.end() ? companion->get() : nullptr;
}

QString ProjectTreeModel::filePath(const QModelIndex &index) const
{
    const Node *node = nodeOf(index);
    return node ? node->path : QString();
}

bool ProjectTreeModel::isDir(const QModelIndex &index) const
{
    const Node *node = nodeOf(index);
    return node && node->isDir;
}

ProjectTreeModel::Node *ProjectTreeModel::nodeOf(const QModelIndex &index) const
{
    return index.isValid() ? static_cast<Node *>(index.internalPointer()) : nullptr;
}

QModelIndex ProjectTreeModel::indexOf(Node *node) const
{
    if (!node || node == invisibleRoot_.get())
        return {};
    return createIndex(node->row, 0, node);
}

QModelIndex ProjectTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    const Node *node = parent.isValid() ? nodeOf(parent) : invisibleRoot_.get();
    if (!node || column != 0 || row < 0 || row >= static_cast<int>(node->children.size()))
        return {};
    return createIndex(row, column, node->children[static_cast<size_t>(row)].get());
}

QModelIndex ProjectTreeModel::parent(const QModelIndex &child) const
{
    const Node *node = nodeOf(child);
    return node ? indexOf(node->parent) : QModelIndex();
}

int ProjectTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;
    const Node *node = parent.isValid() ? nodeOf(parent) : invisibleRoot_.get();
    return node && node->loaded ? static_cast<int>(node->children.size()) : 0;
}

int ProjectTreeModel::columnCount(const QModelIndex &) const
{
    return 1;
}

bool ProjectTreeModel::hasChildren(const QModelIndex &parent) const
{
    const Node *node = parent.isValid() ? nodeOf(parent) : invisibleRoot_.get();
    if (!node)
        return false;
    return node->loaded ? !node->children.empty() : node->isDir;
}

bool ProjectTreeModel::canFetchMore(const QModelIndex &parent) const
{
    const Node *node = nodeOf(parent);
    return node && node->isDir && !node->loaded;
}

void ProjectTreeModel::fetchMore(const QModelIndex &parent)
{
    load(nodeOf(parent));
}

QVariant ProjectTreeModel::data(const QModelIndex &index, int role) const
{
    const Node *node = nodeOf(index);
    if (!node)
        return {};
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return node->name;
    case Qt::DecorationRole:
        return node->isDir ? FileIcons::folder(darkIcons_) : FileIcons::file(node->name, darkIcons_);
    default:
        return {};
    }
}

Qt::ItemFlags ProjectTreeModel::flags(const QModelIndex &index) const
{
    const Node *node = nodeOf(index);
    if (!node)
        return Qt::ItemIsDropEnabled;
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (node->path != rootPath_)
        flags |= Qt::ItemIsDragEnabled;
    if (node->isDir)
        flags |= Qt::ItemIsDropEnabled;
    return flags;
}

QStringList ProjectTreeModel::mimeTypes() const
{
    return {QStringLiteral("text/uri-list")};
}

QMimeData *ProjectTreeModel::mimeData(const QModelIndexList &indexes) const
{
    QList<QUrl> urls;
    for (const QModelIndex &index : indexes) {
        if (const Node *node = nodeOf(index); node && index.column() == 0)
            urls.append(QUrl::fromLocalFile(node->path));
    }
    auto *data = new QMimeData;
    data->setUrls(urls);
    return data;
}

Qt::DropActions ProjectTreeModel::supportedDragActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

Qt::DropActions ProjectTreeModel::supportedDropActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}
