// SPDX-License-Identifier: GPL-3.0-only

#include "TreeDecorations.h"

#include "CommandPalette.h"
#include "FileOperations.h"
#include "FileTreeView.h"
#include "ProjectTreeModel.h"
#include "ui/FileIcons.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QPalette>

#include <algorithm>

namespace {

bool isDarkPalette(const QPalette &palette)
{
    return palette.color(QPalette::Base).lightness() < 128;
}

// Colors of IntelliJ's file states (Darcula and light themes).
QColor statusColor(FileStatus status, bool dark)
{
    switch (status) {
    case FileStatus::Modified:
    case FileStatus::Renamed:
        return QColor::fromRgb(dark ? 0x6897bb : 0x0032a0);
    case FileStatus::Added:
        return QColor::fromRgb(dark ? 0x629755 : 0x067d17);
    case FileStatus::Untracked:
        return QColor::fromRgb(dark ? 0xd1675a : 0x993300);
    case FileStatus::Conflicted:
        return QColor::fromRgb(dark ? 0xd5756c : 0xc00000);
    case FileStatus::Ignored:
        return QColor::fromRgb(dark ? 0x848504 : 0x808000);
    }
    return {};
}

// VS Code's list colors for the files with errors and warnings.
QColor problemColor(const ProblemCount &count, bool dark)
{
    if (count.errors > 0)
        return QColor::fromRgb(dark ? 0xf14c4c : 0xe51400);
    return QColor::fromRgb(dark ? 0xcca700 : 0xbf8803);
}

QString statusName(FileStatus status)
{
    switch (status) {
    case FileStatus::Modified:
        return FileTreeView::tr("modified");
    case FileStatus::Renamed:
        return FileTreeView::tr("renamed");
    case FileStatus::Added:
        return FileTreeView::tr("added");
    case FileStatus::Untracked:
        return FileTreeView::tr("not tracked by git");
    case FileStatus::Conflicted:
        return FileTreeView::tr("in conflict");
    case FileStatus::Ignored:
        return FileTreeView::tr("ignored by git");
    }
    return {};
}

// VS Code's letters for the git states.
QString statusLetter(FileStatus status)
{
    switch (status) {
    case FileStatus::Modified:
        return QStringLiteral("M");
    case FileStatus::Renamed:
        return QStringLiteral("R");
    case FileStatus::Added:
        return QStringLiteral("A");
    case FileStatus::Untracked:
        return QStringLiteral("U");
    case FileStatus::Conflicted:
        return QStringLiteral("C");
    case FileStatus::Ignored:
        return {};
    }
    return {};
}

// Roles that TreeDecorations answers itself, for the first column.
bool isDecoratedRole(int role)
{
    switch (role) {
    case Qt::ForegroundRole:
    case Qt::DecorationRole:
    case Qt::ToolTipRole:
    case TreeRoles::path:
    case TreeRoles::statusLetter:
    case TreeRoles::statusColor:
    case TreeRoles::problemCount:
    case TreeRoles::problemColor:
    case TreeRoles::isOpen:
        return true;
    default:
        return false;
    }
}

// The files' counts added up in each folder that holds them, up to `root`.
ProblemCounts folderProblems(const ProblemCounts &files, const QString &root)
{
    ProblemCounts folders;
    if (root.isEmpty())
        return folders;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        if (!FileOperations::isInside(it.key(), root))
            continue;
        for (QString folder = QFileInfo(it.key()).absolutePath();; folder = QFileInfo(folder).absolutePath()) {
            ProblemCount &sum = folders[folder];
            sum.errors += it->errors;
            sum.warnings += it->warnings;
            if (folder == root)
                break;
        }
    }
    return folders;
}

} // namespace

void TreeDecorations::setRootPath(const QString &rootPath)
{
    rootPath_ = rootPath;
    inIgnored_.clear();
    lastCommits_.clear();
    folderProblems_ = folderProblems(problems_, rootPath_);
}

void TreeDecorations::setStatuses(const FileStatusMap &statuses)
{
    statuses_ = statuses;
    folderStatuses_ = FileStatuses::folderStatuses(statuses, rootPath_);
    hasIgnored_ = std::ranges::any_of(statuses, [](FileStatus s) { return s == FileStatus::Ignored; });
    inIgnored_.clear();
    lastCommits_.clear(); // a commit, a checkout… may have changed them
}

void TreeDecorations::setProblems(const ProblemCounts &problems)
{
    problems_ = problems;
    folderProblems_ = folderProblems(problems_, rootPath_);
}

void TreeDecorations::setLastCommit(const QString &path, const QString &description)
{
    lastCommits_.insert(path, description);
}

void TreeDecorations::setScope(const std::optional<QSet<QString>> &files, const QSet<QString> &ancestors)
{
    beginFilterChange();
    scopeFiles_ = files;
    scopeAncestors_ = ancestors;
    endFilterChange(Direction::Rows);
}

void TreeDecorations::setNameFilter(const QString &text)
{
    if (text == nameFilter_)
        return;
    beginFilterChange();
    nameFilter_ = text;
    // The folders leading to a match stay, whatever their names.
    setRecursiveFilteringEnabled(!text.isEmpty());
    endFilterChange(Direction::Rows);
}

std::optional<FileStatus> TreeDecorations::statusOf(const QString &path, bool isDir) const
{
    if (const auto it = statuses_.constFind(path); it != statuses_.cend())
        return *it;
    if (isDir) {
        if (const auto it = folderStatuses_.constFind(path); it != folderStatuses_.cend())
            return *it;
    }
    if (hasIgnored_ && isInIgnoredFolder(path))
        return FileStatus::Ignored;
    return std::nullopt;
}

bool TreeDecorations::isInIgnoredFolder(const QString &path) const
{
    if (const auto it = inIgnored_.constFind(path); it != inIgnored_.cend())
        return *it;
    bool ignored = false;
    for (qsizetype slash = path.lastIndexOf(QLatin1Char('/')); slash > rootPath_.size();
         slash = path.lastIndexOf(QLatin1Char('/'), slash - 1)) {
        if (statuses_.value(path.left(slash), FileStatus::Modified) == FileStatus::Ignored) {
            ignored = true;
            break;
        }
    }
    inIgnored_.insert(path, ignored);
    return ignored;
}

QVariant TreeDecorations::data(const QModelIndex &index, int role) const
{
    if (index.column() != 0 || !isDecoratedRole(role))
        return QSortFilterProxyModel::data(index, role);
    const auto *model = static_cast<ProjectTreeModel *>(sourceModel());
    const QModelIndex source = mapToSource(index);
    if (role == TreeRoles::path)
        return model->filePath(source);
    const QString path = model->filePath(source);
    const bool isDir = model->isDir(source);
    const bool dark = isDarkPalette(QGuiApplication::palette());
    switch (role) {
    case TreeRoles::statusLetter:
        return statusLetterOf(path, isDir);
    case TreeRoles::statusColor:
        if (const std::optional<FileStatus> status = statusOf(path, isDir))
            return statusColor(*status, dark);
        return {};
    case TreeRoles::problemCount: {
        const ProblemCount count = isDir ? ProblemCount() : problems_.value(path);
        return count.errors + count.warnings;
    }
    case TreeRoles::problemColor:
        if (const std::optional<ProblemCount> count = problemsOf(path, isDir))
            return problemColor(*count, dark);
        return {};
    case TreeRoles::isOpen:
        return openFiles_.contains(path);
    case Qt::ToolTipRole:
        return toolTipOf(path, isDir);
    case Qt::ForegroundRole:
        if (const std::optional<QColor> color = foregroundOf(path, isDir))
            return *color;
        break;
    case Qt::DecorationRole:
        if (touched_.contains(path))
            return FileIcons::withClaudeDot(QSortFilterProxyModel::data(index, role).value<QIcon>());
        break;
    default:
        break;
    }
    return QSortFilterProxyModel::data(index, role);
}

bool TreeDecorations::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const auto *model = static_cast<ProjectTreeModel *>(sourceModel());
    const QModelIndex source = model->index(sourceRow, 0, sourceParent);
    const QString path = model->filePath(source);
    if (path == rootPath_)
        return true;
    if (nameFilter_.isEmpty())
        return isInScope(path);
    // The folders leading to the matches come with recursive filtering.
    const bool isScopeFile = !scopeFiles_ || scopeFiles_->contains(path);
    return isScopeFile && CommandPalette::matchScore(source.data(Qt::DisplayRole).toString(), nameFilter_) >= 0;
}

bool TreeDecorations::isInScope(const QString &path) const
{
    return !scopeFiles_ || scopeFiles_->contains(path) || scopeAncestors_.contains(path);
}

std::optional<ProblemCount> TreeDecorations::problemsOf(const QString &path, bool isDir) const
{
    const ProblemCounts &counts = isDir ? folderProblems_ : problems_;
    if (const auto it = counts.constFind(path); it != counts.cend())
        return *it;
    return std::nullopt;
}

// Files only: a folder's color says enough.
QString TreeDecorations::statusLetterOf(const QString &path, bool isDir) const
{
    const std::optional<FileStatus> status = statusOf(path, isDir);
    return status && !isDir ? statusLetter(*status) : QString();
}

// The cut entries are dimmed; then, as in VS Code, problems come before the
// git state.
std::optional<QColor> TreeDecorations::foregroundOf(const QString &path, bool isDir) const
{
    const bool isCut =
        std::ranges::any_of(cut_, [&path](const QString &entry) { return FileOperations::isInside(path, entry); });
    if (isCut)
        return QGuiApplication::palette().color(QPalette::Disabled, QPalette::Text);
    const bool dark = isDarkPalette(QGuiApplication::palette());
    if (const std::optional<ProblemCount> count = problemsOf(path, isDir))
        return problemColor(*count, dark);
    if (const std::optional<FileStatus> status = statusOf(path, isDir))
        return statusColor(*status, dark);
    return std::nullopt;
}

QString TreeDecorations::toolTipOf(const QString &path, bool isDir) const
{
    QStringList lines{QDir::toNativeSeparators(path)};
    const QFileInfo info(path);
    const QLocale locale;
    const QString modified = locale.toString(info.lastModified(), QLocale::ShortFormat);
    lines.append(isDir ? FileTreeView::tr("Modified %1").arg(modified)
                       : FileTreeView::tr("%1, modified %2").arg(locale.formattedDataSize(info.size()), modified));
    if (const std::optional<FileStatus> status = statusOf(path, isDir)) {
        // A folder colored by the files it contains.
        const bool contains = isDir && !statuses_.contains(path) && *status != FileStatus::Ignored;
        lines.append(contains ? FileTreeView::tr("contains changes") : statusName(*status));
    }
    lines += problemLines(path, isDir);
    if (touched_.contains(path))
        lines.append(FileTreeView::tr("just edited by Claude"));
    if (const auto it = lastCommits_.constFind(path); it != lastCommits_.cend() && !it->isEmpty())
        lines.append(FileTreeView::tr("Last commit: %1").arg(*it));
    return lines.join(QLatin1Char('\n'));
}

QStringList TreeDecorations::problemLines(const QString &path, bool isDir) const
{
    const std::optional<ProblemCount> count = problemsOf(path, isDir);
    if (!count)
        return {};
    QStringList lines;
    if (count->errors > 0)
        lines.append(isDir ? FileTreeView::tr("%n error(s) in its files", nullptr, count->errors)
                           : FileTreeView::tr("%n error(s)", nullptr, count->errors));
    if (count->warnings > 0)
        lines.append(isDir ? FileTreeView::tr("%n warning(s) in its files", nullptr, count->warnings)
                           : FileTreeView::tr("%n warning(s)", nullptr, count->warnings));
    return lines;
}
