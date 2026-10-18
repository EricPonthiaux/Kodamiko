#include "ProblemsView.h"

#include "ProblemStore.h"
#include "ui/TreeColumnFit.h"
#include "workspace/FileIcons.h"

#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int refreshDelayMs = 200;

enum ItemRole {
    PathRole = Qt::UserRole,
    LineRole,
    ColumnRole
};

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

} // namespace

ProblemsView::ProblemsView(ProblemStore *problems, QWidget *parent)
    : QWidget(parent)
    , problems_(problems)
    , summary_(new QLabel(this))
    , tree_(new QTreeWidget(this))
    , placeholder_(new QLabel(tr("No problems"), this))
    , stack_(new QStackedWidget(this))
{
    summary_->setEnabled(false);
    summary_->setContentsMargins(8, 4, 8, 4);

    tree_->header()->hide();
    tree_->setUniformRowHeights(true);
    tree_->setAnimated(false);
    tree_->setExpandsOnDoubleClick(false);
    columnFit_ = new TreeColumnFit(tree_);
    connect(tree_, &QTreeWidget::itemClicked, this, &ProblemsView::activate);
    connect(tree_, &QTreeWidget::itemActivated, this, &ProblemsView::activate);
    connect(tree_, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem *item) { collapsedFiles_.insert(item->data(0, PathRole).toString()); });
    connect(tree_, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) { collapsedFiles_.remove(item->data(0, PathRole).toString()); });

    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setEnabled(false);
    stack_->addWidget(placeholder_);
    stack_->addWidget(tree_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(summary_);
    layout->addWidget(stack_, 1);

    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(refreshDelayMs);
    connect(&refreshTimer_, &QTimer::timeout, this, &ProblemsView::refresh);
    connect(problems_, &ProblemStore::changed, this, &ProblemsView::scheduleRefresh);
    refresh();
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

void ProblemsView::scheduleRefresh()
{
    if (!refreshTimer_.isActive())
        refreshTimer_.start();
}

void ProblemsView::refresh()
{
    refreshTimer_.stop();

    struct File {
        QString path;
        Problems problems;
        int errors = 0;
    };

    QList<File> files;
    int errors = 0;
    int warnings = 0;
    for (const QString &path : problems_->files()) {
        File file{path, problems_->problems(path)};
        if (file.problems.isEmpty())
            continue;
        std::stable_sort(file.problems.begin(), file.problems.end(), [](const Problem &a, const Problem &b) {
            if (a.severity != b.severity)
                return a.severity == Diagnostic::Severity::Error;
            return std::pair(a.line, a.column) < std::pair(b.line, b.column);
        });
        for (const Problem &problem : std::as_const(file.problems))
            file.errors += problem.severity == Diagnostic::Severity::Error;
        errors += file.errors;
        warnings += file.problems.size() - file.errors;
        files.append(file);
    }
    std::stable_sort(files.begin(), files.end(), [](const File &a, const File &b) { return a.errors > b.errors; });

    // The selected problem stays selected when it is still there.
    QString selectedPath;
    int selectedLine = -1;
    QString selectedMessage;
    if (const QTreeWidgetItem *current = tree_->currentItem()) {
        selectedPath = current->data(0, PathRole).toString();
        selectedLine = current->parent() ? current->data(0, LineRole).toInt() : -1;
        selectedMessage = current->parent() ? current->toolTip(0) : QString();
    }
    const int scroll = tree_->verticalScrollBar() ? tree_->verticalScrollBar()->value() : 0;

    tree_->setUpdatesEnabled(false);
    tree_->clear();
    QTreeWidgetItem *selected = nullptr;
    const QDir root(folder_);
    for (const File &file : std::as_const(files)) {
        const QFileInfo info(file.path);
        QString folder = folder_.isEmpty() ? info.absolutePath() : root.relativeFilePath(info.absolutePath());
        if (folder == QLatin1String("."))
            folder.clear();
        auto *fileItem = new QTreeWidgetItem(tree_);
        fileItem->setText(0, QStringLiteral("%1  %2").arg(info.fileName(),
                                                          countText(file.errors, file.problems.size() - file.errors)));
        fileItem->setIcon(0, FileIcons::file(info.fileName(), darkIcons_));
        fileItem->setToolTip(0, QDir::toNativeSeparators(
                                    folder.isEmpty() ? info.fileName() : folder + QLatin1Char('/') + info.fileName()));
        fileItem->setData(0, PathRole, file.path);
        if (selectedLine < 0 && file.path == selectedPath)
            selected = fileItem;
        for (const Problem &problem : file.problems) {
            auto *item = new QTreeWidgetItem(fileItem);
            const QString firstLine = problem.message.section(QLatin1Char('\n'), 0, 0);
            item->setText(0, QStringLiteral("%1  :%2").arg(firstLine).arg(problem.line + 1));
            item->setIcon(0, severityIcon(problem.severity));
            QString toolTip = problem.message;
            if (!problem.source.isEmpty())
                toolTip += QStringLiteral("\n— %1").arg(problem.source);
            item->setToolTip(0, toolTip);
            item->setData(0, PathRole, file.path);
            item->setData(0, LineRole, problem.line);
            item->setData(0, ColumnRole, problem.column);
            if (!selected && file.path == selectedPath && problem.line == selectedLine && toolTip == selectedMessage)
                selected = item;
        }
        fileItem->setExpanded(!collapsedFiles_.contains(file.path));
    }
    if (selected)
        tree_->setCurrentItem(selected);
    if (tree_->verticalScrollBar())
        tree_->verticalScrollBar()->setValue(scroll);
    tree_->setUpdatesEnabled(true);
    columnFit_->refit();

    stack_->setCurrentWidget(files.isEmpty() ? static_cast<QWidget *>(placeholder_) : tree_);
    summary_->setText(files.isEmpty() ? QString() : countText(errors, warnings));
    summary_->setVisible(!files.isEmpty());
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
    emit problemActivated(item->data(0, PathRole).toString(), item->data(0, LineRole).toInt(),
                          item->data(0, ColumnRole).toInt());
}
