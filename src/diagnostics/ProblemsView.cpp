// SPDX-License-Identifier: GPL-3.0-only

#include "ProblemsView.h"

#include "ProblemStore.h"
#include "ui/ContextMenus.h"
#include "ui/FileIcons.h"
#include "ui/TreeColumnFit.h"
#include "ui/ViewHeader.h"

#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int refreshDelayMs = 200;

// Item data roles.
constexpr int pathRole = Qt::UserRole;
constexpr int lineRole = Qt::UserRole + 1;
constexpr int columnRole = Qt::UserRole + 2;

// A red disc with "!" for an error, an amber triangle for a warning.
QIcon severityIcon(Diagnostic::Severity severity)
{
    static QIcon error;
    static QIcon warning;
    QIcon &icon = severity == Diagnostic::Severity::Error ? error : warning;
    if (!icon.isNull())
        return icon;
    for (const int size : {16, 32}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        const qreal unit = size / 16.0;
        if (severity == Diagnostic::Severity::Error) {
            painter.setBrush(QColor("#e5534b"));
            painter.drawEllipse(QRectF(2 * unit, 2 * unit, 12 * unit, 12 * unit));
        } else {
            painter.setBrush(QColor("#d4a72c"));
            painter.drawPolygon(QPolygonF(
                {QPointF(8 * unit, 1.5 * unit), QPointF(15 * unit, 14 * unit), QPointF(1 * unit, 14 * unit)}));
        }
        painter.setPen(QPen(Qt::white, 1.6 * unit, Qt::SolidLine, Qt::RoundCap));
        const qreal top = severity == Diagnostic::Severity::Error ? 4.8 : 5.6;
        painter.drawLine(QPointF(8 * unit, top * unit), QPointF(8 * unit, 9 * unit));
        painter.drawPoint(QPointF(8 * unit, 11.4 * unit));
        icon.addPixmap(pixmap);
    }
    return icon;
}

QString countText(int errors, int warnings)
{
    QStringList parts;
    if (errors > 0)
        parts.append(ProblemsView::tr("%n error(s)", nullptr, errors));
    if (warnings > 0)
        parts.append(ProblemsView::tr("%n warning(s)", nullptr, warnings));
    return parts.join(QStringLiteral(", "));
}

// A file's problems, errors first, then by position.
struct FileProblems {
    QString path;
    Problems problems;
    int errors = 0;

    int warnings() const { return int(problems.size()) - errors; }
};

// The files with problems, those with the most errors first.
QList<FileProblems> sortedFiles(const ProblemStore &store)
{
    QList<FileProblems> files;
    for (const QString &path : store.files()) {
        FileProblems file{path, store.problems(path)};
        if (file.problems.isEmpty())
            continue;
        std::stable_sort(file.problems.begin(), file.problems.end(), [](const Problem &a, const Problem &b) {
            if (a.severity != b.severity)
                return a.severity == Diagnostic::Severity::Error;
            return std::pair(a.line, a.column) < std::pair(b.line, b.column);
        });
        file.errors = int(std::count_if(file.problems.begin(), file.problems.end(), [](const Problem &problem) {
            return problem.severity == Diagnostic::Severity::Error;
        }));
        files.append(file);
    }
    std::stable_sort(files.begin(), files.end(),
                     [](const FileProblems &a, const FileProblems &b) { return a.errors > b.errors; });
    return files;
}

// The selected item, found again after the tree is rebuilt.
struct Selection {
    QString path;
    int line = -1; // -1 when a file is selected
    QString message;

    bool isFile(const QString &filePath) const { return line < 0 && filePath == path; }

    bool isProblem(const QString &filePath, int problemLine, const QString &toolTip) const
    {
        return filePath == path && problemLine == line && toolTip == message;
    }
};

Selection selectionOf(const QTreeWidgetItem *current)
{
    if (!current)
        return {};
    const bool isProblem = current->parent() != nullptr;
    return {current->data(0, pathRole).toString(), isProblem ? current->data(0, lineRole).toInt() : -1,
            isProblem ? current->toolTip(0) : QString()};
}

QString problemToolTip(const Problem &problem)
{
    if (problem.source.isEmpty())
        return problem.message;
    return problem.message + QStringLiteral("\n— %1").arg(problem.source);
}

QTreeWidgetItem *addProblemItem(QTreeWidgetItem *fileItem, const QString &path, const Problem &problem)
{
    auto *item = new QTreeWidgetItem(fileItem);
    const QString firstLine = problem.message.section(QLatin1Char('\n'), 0, 0);
    item->setText(0, QStringLiteral("%1  :%2").arg(firstLine).arg(problem.line + 1));
    item->setIcon(0, severityIcon(problem.severity));
    item->setToolTip(0, problemToolTip(problem));
    item->setData(0, pathRole, path);
    item->setData(0, lineRole, problem.line);
    item->setData(0, columnRole, problem.column);
    return item;
}

// Adds the file's problems; returns the one that was selected, or null.
QTreeWidgetItem *addProblemItems(QTreeWidgetItem *fileItem, const FileProblems &file, const Selection &selection)
{
    QTreeWidgetItem *selected = nullptr;
    for (const Problem &problem : file.problems) {
        QTreeWidgetItem *item = addProblemItem(fileItem, file.path, problem);
        if (!selected && selection.isProblem(file.path, problem.line, item->toolTip(0)))
            selected = item;
    }
    return selected;
}

// `projectFolder` empty: folders are shown absolute.
QTreeWidgetItem *addFileItem(QTreeWidget *tree, const FileProblems &file, const QString &projectFolder, bool darkIcons)
{
    const QFileInfo info(file.path);
    QString folder =
        projectFolder.isEmpty() ? info.absolutePath() : QDir(projectFolder).relativeFilePath(info.absolutePath());
    if (folder == QLatin1String("."))
        folder.clear();
    auto *fileItem = new QTreeWidgetItem(tree);
    fileItem->setText(0, QStringLiteral("%1  %2").arg(info.fileName(), countText(file.errors, file.warnings())));
    fileItem->setIcon(0, FileIcons::file(info.fileName(), darkIcons));
    fileItem->setToolTip(
        0, QDir::toNativeSeparators(folder.isEmpty() ? info.fileName() : folder + QLatin1Char('/') + info.fileName()));
    fileItem->setData(0, pathRole, file.path);
    return fileItem;
}

} // namespace

ProblemsView::ProblemsView(ProblemStore *problems, QWidget *parent)
    : QWidget(parent)
    , problems_(problems)
    , header_(new ViewHeader(tr("Problems"), this))
    , tree_(new QTreeWidget(this))
    , placeholder_(new QLabel(tr("No problems"), this))
    , stack_(new QStackedWidget(this))
{
    connect(header_->addButton(QStringLiteral("expand-all"), tr("Expand All")), &QToolButton::clicked, this,
            [this] { setAllExpanded(true); });
    connect(header_->addButton(QStringLiteral("collapse-all"), tr("Collapse All")), &QToolButton::clicked, this,
            [this] { setAllExpanded(false); });

    tree_->header()->hide();
    tree_->setUniformRowHeights(true);
    tree_->setAnimated(false);
    tree_->setExpandsOnDoubleClick(false);
    columnFit_ = new TreeColumnFit(tree_);
    connect(tree_, &QTreeWidget::itemClicked, this, &ProblemsView::activate);
    connect(tree_, &QTreeWidget::itemActivated, this, &ProblemsView::activate);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QWidget::customContextMenuRequested, this, &ProblemsView::showContextMenu);
    connect(tree_, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem *item) { collapsedFiles_.insert(item->data(0, pathRole).toString()); });
    connect(tree_, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) { collapsedFiles_.remove(item->data(0, pathRole).toString()); });

    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setEnabled(false);
    stack_->addWidget(placeholder_);
    stack_->addWidget(tree_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(stack_, 1);

    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(refreshDelayMs);
    connect(&refreshTimer_, &QTimer::timeout, this, &ProblemsView::refresh);
    connect(problems_, &ProblemStore::changed, this, &ProblemsView::scheduleRefresh);
    refresh();
}

Problems ProblemsView::selectedProblems() const
{
    const QTreeWidgetItem *current = tree_->currentItem();
    if (!current)
        return {};
    const Problems problems = problems_->problems(current->data(0, pathRole).toString());
    const bool isFile = current->parent() == nullptr;
    if (isFile)
        return problems;
    const Selection selection = selectionOf(current);
    const auto selected = std::ranges::find_if(problems, [&selection](const Problem &problem) {
        return selection.isProblem(problem.path, problem.line, problemToolTip(problem));
    });
    if (selected == problems.end())
        return {};
    return {*selected};
}

void ProblemsView::showContextMenu(const QPoint &position)
{
    QTreeWidgetItem *item = tree_->itemAt(position);
    if (!item || contextActions_.isEmpty())
        return;
    tree_->setCurrentItem(item);
    QMenu menu(tree_);
    menu.addActions(contextActions_);
    ContextMenus::exec(menu, *tree_, tree_->viewport()->mapToGlobal(position));
}

void ProblemsView::setProjectFolder(const QString &folder)
{
    folder_ = folder;
    collapsedFiles_.clear();
    refresh();
}

void ProblemsView::setDarkIcons(bool dark)
{
    if (dark == darkIcons_)
        return;
    darkIcons_ = dark;
    refresh();
}

void ProblemsView::setIconColor(const QColor &color)
{
    header_->setIconColor(color);
}

void ProblemsView::setAllExpanded(bool expanded)
{
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        tree_->topLevelItem(i)->setExpanded(expanded);
}

void ProblemsView::scheduleRefresh()
{
    if (!refreshTimer_.isActive())
        refreshTimer_.start();
}

void ProblemsView::refresh()
{
    refreshTimer_.stop();

    const QList<FileProblems> files = sortedFiles(*problems_);
    int errors = 0;
    int warnings = 0;
    for (const FileProblems &file : files) {
        errors += file.errors;
        warnings += file.warnings();
    }

    // The selected problem stays selected when it is still there.
    const Selection selection = selectionOf(tree_->currentItem());
    const int scroll = tree_->verticalScrollBar() ? tree_->verticalScrollBar()->value() : 0;

    tree_->setUpdatesEnabled(false);
    tree_->clear();
    QTreeWidgetItem *selected = nullptr;
    for (const FileProblems &file : files) {
        QTreeWidgetItem *fileItem = addFileItem(tree_, file, folder_, darkIcons_);
        QTreeWidgetItem *selectedProblem = addProblemItems(fileItem, file, selection);
        if (selection.isFile(file.path))
            selected = fileItem;
        else if (!selected)
            selected = selectedProblem;
        fileItem->setExpanded(!collapsedFiles_.contains(file.path));
    }
    if (selected)
        tree_->setCurrentItem(selected);
    if (tree_->verticalScrollBar())
        tree_->verticalScrollBar()->setValue(scroll);
    tree_->setUpdatesEnabled(true);
    columnFit_->refit();

    stack_->setCurrentWidget(files.isEmpty() ? static_cast<QWidget *>(placeholder_) : tree_);
    header_->setDetail(countText(errors, warnings));
    if (errors != errors_ || warnings != warnings_) {
        errors_ = errors;
        warnings_ = warnings;
        emit countsChanged(errors, warnings);
    }
}

void ProblemsView::activate(QTreeWidgetItem *item)
{
    if (!item)
        return;
    if (!item->parent()) {
        // A file: its first problem.
        if (item->childCount() == 0)
            return;
        item = item->child(0);
    }
    emit problemActivated(item->data(0, pathRole).toString(), item->data(0, lineRole).toInt(),
                          item->data(0, columnRole).toInt());
}
