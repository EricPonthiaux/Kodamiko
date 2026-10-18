#pragma once

#include "git/FileStatuses.h"

#include <QColor>
#include <QHash>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStringList>

#include <optional>

// Roles of the file tree's rows beyond Qt's, answered for the first column.
namespace TreeRoles {

constexpr int path = Qt::UserRole + 100;         // QString, absolute
constexpr int statusLetter = Qt::UserRole + 101; // QString: the git state as one letter (M, A, U…)
constexpr int statusColor = Qt::UserRole + 102;  // QColor of that letter
constexpr int problemCount = Qt::UserRole + 103; // int: errors and warnings of a file
constexpr int problemColor = Qt::UserRole + 104; // QColor of that count
constexpr int isOpen = Qt::UserRole + 105;       // bool: the file has a tab in the editor

} // namespace TreeRoles

// The errors and warnings of a file.
struct ProblemCount {
    int errors = 0;
    int warnings = 0;
    bool operator==(const ProblemCount &) const = default;
};

// By absolute path.
using ProblemCounts = QHash<QString, ProblemCount>;

// What the file tree adds to the project model, as a proxy over a
// ProjectTreeModel: the colors of the git states and of the problems
// (folders too, from the files they contain), Claude's dot, the files open
// in the editor, the dimming of the cut files, and a tooltip with the
// file's size, date and last commit. It also filters the rows: those of a
// scope, and those whose name matches a filter (with the folders leading to
// them; among the folders read so far).
class TreeDecorations : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setRootPath(const QString &rootPath);
    void setStatuses(const FileStatusMap &statuses);
    void setProblems(const ProblemCounts &problems);

    void setTouched(const QSet<QString> &paths) { touched_ = paths; }

    void setOpenFiles(const QSet<QString> &paths) { openFiles_ = paths; }

    // Shown in the tooltip, once known; forgotten with the statuses.
    void setLastCommit(const QString &path, const QString &description);

    bool hasLastCommit(const QString &path) const { return lastCommits_.contains(path); }

    // The entries cut to the clipboard, dimmed until pasted.
    void setCut(const QStringList &paths) { cut_ = paths; }

    QStringList cut() const { return cut_; }

    // nullopt: every row; otherwise only these files and the rows that
    // lead to them (folders, main files of nested ones).
    void setScope(const std::optional<QSet<QString>> &files, const QSet<QString> &ancestors);
    // Rows whose name matches `text` as the speed search matches it; empty:
    // every row.
    void setNameFilter(const QString &text);

    std::optional<FileStatus> statusOf(const QString &path, bool isDir) const;

    QVariant data(const QModelIndex &index, int role) const override;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    // Asked for each role of each painted row: the walk up the folders is
    // remembered until the statuses change.
    bool isInIgnoredFolder(const QString &path) const;
    bool isInScope(const QString &path) const;
    std::optional<ProblemCount> problemsOf(const QString &path, bool isDir) const;
    QString statusLetterOf(const QString &path, bool isDir) const;
    std::optional<QColor> foregroundOf(const QString &path, bool isDir) const;
    QString toolTipOf(const QString &path, bool isDir) const;
    QStringList problemLines(const QString &path, bool isDir) const;

    QString rootPath_;
    QStringList cut_;
    FileStatusMap statuses_;
    FileStatusMap folderStatuses_;
    bool hasIgnored_ = false;
    mutable QHash<QString, bool> inIgnored_; // isInIgnoredFolder(), by path
    ProblemCounts problems_;
    ProblemCounts folderProblems_; // the sums of the files each folder holds
    QSet<QString> touched_;
    QSet<QString> openFiles_;
    QHash<QString, QString> lastCommits_;
    std::optional<QSet<QString>> scopeFiles_;
    QSet<QString> scopeAncestors_;
    QString nameFilter_;
};
