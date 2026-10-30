// SPDX-License-Identifier: GPL-3.0-only

#include "MergeView.h"

#include "ConflictNavigator.h"
#include "MergeChoicePanel.h"
#include "MergeProposals.h"
#include "MergeReadyPanel.h"
#include "MergeTextPanel.h"
#include "MergeTexts.h"
#include "agent/AgentProvider.h"
#include "changes/DiffView.h"
#include "git/GitCommands.h"
#include "git/ThreeWayMerge.h"
#include "ui/AppFonts.h"
#include "ui/GripSplitter.h"
#include "ui/PopupWidgets.h"
#include "ui/UiIcons.h"
#include "ui/ViewHeader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int listWidthPx = 260;
constexpr int listMinimumWidthPx = 160;
const QString sidebarKey = QStringLiteral("mergeWindow/sidebar");

using ConflictVersions::Kind;

bool isText(const ConflictVersions::File &file)
{
    const bool hasBothSides = file.kind == Kind::BothModified || file.kind == Kind::BothAdded;
    return hasBothSides && !file.isBinary;
}

// Empty for a version the file does not have.
QByteArray contentOf(const std::optional<ConflictVersions::Version> &version)
{
    return version ? version->content : QByteArray();
}

} // namespace

MergeView::MergeView(QString folder, Theme theme, GitCommands *commands, QWidget *parent)
    : QWidget(parent)
    , folder_(std::move(folder))
    , theme_(std::move(theme))
    , commands_(commands)
    , proposals_(new MergeProposals(this))
    , list_(new QListWidget(this))
    , message_(new QLabel(this))
{
    pool_.setMaxThreadCount(1);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setWordWrap(true);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(list_, &QListWidget::currentRowChanged, this, &MergeView::showFile);
    message_->setObjectName(QStringLiteral("pageDescription"));
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);
    connect(commands_, &GitCommands::busyChanged, this, [this](bool busy) {
        working_->setEnabled(!busy);
        updateFooter();
    });
    connectProposals();

    auto *fileArea = new QWidget(this);
    fileArea_ = new QVBoxLayout(fileArea);
    fileArea_->setContentsMargins(0, 0, 0, 0);
    fileArea_->addWidget(message_);

    auto *workbench = new QWidget(this);
    auto *workbenchLayout = new QVBoxLayout(workbench);
    workbenchLayout->setContentsMargins(0, 0, 0, 0);
    workbenchLayout->setSpacing(0);
    workbenchLayout->addWidget(createBar());
    workbenchLayout->addWidget(fileArea, 1);
    QWidget *sidebar = createSidebar();
    sidebar->setMinimumWidth(listMinimumWidthPx);
    auto *body = new GripSplitter(Qt::Horizontal, this);
    body->setChildrenCollapsible(false);
    body->addWidget(sidebar);
    body->addWidget(workbench);
    body->setStretchFactor(1, 1);
    if (!body->restoreState(QSettings().value(sidebarKey).toByteArray()))
        body->setSizes({listWidthPx, 4 * listWidthPx});
    connect(body, &QSplitter::splitterMoved, this, [body] { QSettings().setValue(sidebarKey, body->saveState()); });
    working_ = body;
    pages_ = new QStackedWidget(this);
    pages_->addWidget(working_);
    ready_ = new MergeReadyPanel(this);
    connect(ready_, &MergeReadyPanel::messageChanged, this, &MergeView::updateFooter);
    pages_->addWidget(ready_);
    review_ = new DiffView(theme_, this);
    review_->setObjectName(QStringLiteral("mergeReview"));
    pages_->addWidget(review_);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createTitleBar());
    layout->addWidget(pages_, 1);
    layout->addWidget(createFooter());
    showMessage(tr("Reading the conflicts…"));
    applyTheme(theme_);
    reload();
}

MergeView::~MergeView() = default;

void MergeView::applyTheme(const Theme &theme)
{
    theme_ = theme;
    setStyleSheet(popupStyleSheet(
        theme, QStringLiteral("QWidget#mergeSidebar { background: @base; }"
                              "QWidget#mergeSidebar QListWidget { padding: 4px 6px; }"
                              "QLabel#mergeLegend { color: @muted; padding: 8px 12px 12px; }"
                              "QWidget#mergeFooter { border-top: 1px solid @line; }"
                              "QLabel#mergeReadyFiles { padding: 12px 0; border-top: 1px solid @line; border-bottom: "
                              "1px solid @line; }")));
    filesHeader_->setIconColor(theme.foreground);
    fileHeader_->setIconColor(theme.foreground);
    navigator_->applyTheme(theme);
    ready_->applyTheme(theme);
    review_->applyTheme(theme);
    if (panel_)
        panel_->applyTheme(theme);
    if (choices_)
        choices_->applyTheme(theme);
}

void MergeView::showConflicts(const Conflicts &conflicts)
{
    // Reloads follow every change of the repository: most change nothing.
    if (finalizing_ || (hasRead_ && conflicts == conflicts_))
        return;
    const bool ended = Integration::canContinue(conflicts_.repository.operation) &&
                       conflicts.repository.operation.isEmpty() && conflicts.files.isEmpty() &&
                       conflicts.error.isEmpty();
    hasRead_ = true;
    conflicts_ = conflicts;
    if (conflicts.completedMerge) {
        emit mergeCompleted(
            {.operation = tr("merge"), .sides = conflicts.sides, .commitId = conflicts.repository.headId});
        return;
    }
    if (ended) {
        emit operationEnded();
        return;
    }
    rememberPaths(conflicts);
    std::erase_if(resolutions_, [&conflicts](const auto &entry) {
        return std::ranges::find(conflicts.files, entry.second.file) == conflicts.files.end();
    });
    title_->setText(MergeTexts::title(conflicts.repository.operation, conflicts.sides));
    const QString selected = list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString() : QString();
    rebuildFiles();
    showConflictState(selected);
}

void MergeView::rememberPaths(const Conflicts &conflicts)
{
    for (const QString &path : conflicts.paths) {
        if (!mergedPaths_.contains(path))
            mergedPaths_.append(path);
    }
    for (const ConflictVersions::File &file : conflicts.files) {
        if (!mergedPaths_.contains(file.path))
            mergedPaths_.append(file.path);
    }
}

void MergeView::rebuildFiles()
{
    {
        const QSignalBlocker block(list_);
        list_->clear();
        for (const ConflictVersions::File &file : std::as_const(conflicts_.files)) {
            auto *item = new QListWidgetItem(list_);
            item->setData(Qt::UserRole, file.path);
            item->setToolTip(file.path);
        }
    }
    updateList();
}

void MergeView::showConflictState(const QString &selected)
{
    const Conflicts &conflicts = conflicts_;
    if (!conflicts.error.isEmpty()) {
        pages_->setCurrentWidget(working_);
        showFile(-1);
        showMessage(conflicts.error);
    } else if (conflicts_.files.isEmpty()) {
        showFile(-1);
        showMessage(conflicts.repository.operation.isEmpty() ? tr("No conflict is left.")
                                                             : tr("Every conflict is resolved."));
        if (Integration::canContinue(conflicts.repository.operation))
            showReady();
        else
            pages_->setCurrentWidget(working_);
    } else {
        pages_->setCurrentWidget(working_);
        const auto found = std::ranges::find(conflicts_.files, selected, &ConflictVersions::File::path);
        // From no row after the clear: showFile() follows.
        list_->setCurrentRow(found == conflicts_.files.end() ? 0 : int(found - conflicts_.files.begin()));
    }
    updateBar();
    updateFooter();
}

void MergeView::showFile(int row)
{
    for (QWidget *shown : {static_cast<QWidget *>(panel_), static_cast<QWidget *>(choices_)}) {
        if (shown) {
            shown->hide();
            shown->deleteLater();
        }
    }
    panel_ = nullptr;
    choices_ = nullptr;
    updateFooter();
    if (row < 0 || row >= conflicts_.files.size())
        return;
    const ConflictVersions::File &file = conflicts_.files[row];
    fileTitle_->setText(file.path);
    fileTitle_->setToolTip(file.path);
    MergeDocument *document = documentOf(file);
    message_->hide();
    if (!document) {
        showChoices(file);
        updateBar();
        updateFooter();
        return;
    }
    panel_ =
        new MergeTextPanel(theme_, {file.path, document, conflicts_.sides, agent_ ? agent_->name() : QString()}, this);
    connect(panel_, &MergeTextPanel::documentChanged, this, [this] {
        updateList();
        updateBar();
        updateFooter();
    });
    connect(panel_, &MergeTextPanel::conflictsChanged, this, &MergeView::updateNavigator);
    connect(panel_, &MergeTextPanel::conflictResolved, this, &MergeView::onConflictResolved);
    fileArea_->addWidget(panel_);
    panel_->setFocus();
    updateList();
    updateBar();
    updateFooter();
}

void MergeView::showChoices(const ConflictVersions::File &file)
{
    choices_ = new MergeChoicePanel(theme_, file, conflicts_.sides, this);
    connect(choices_, &MergeChoicePanel::sideChosen, this, [this, path = file.path](Integration::ConflictSide side) {
        resolve(tr("Resolving %1").arg(path), [repository = conflicts_.repository, path, side] {
            Integration::acceptVersion(repository, {path}, side);
            return tr("%1 is resolved.").arg(path);
        });
    });
    connect(choices_, &MergeChoicePanel::nameChosen, this, [this, file](const QString &kept) {
        const QStringList paths{file.path, file.current ? file.current->path : QString(),
                                file.incoming ? file.incoming->path : QString()};
        resolve(tr("Keeping %1").arg(kept),
                [repository = conflicts_.repository, paths, kept, content = contentOf(file.current)] {
                    Integration::resolveRename(repository, paths, kept, content);
                    return tr("%1 is resolved.").arg(kept);
                });
    });
    fileArea_->addWidget(choices_);
}

void MergeView::resolve(const QString &title, const std::function<QString()> &job)
{
    commands_->run(title, job, [view = QPointer(this)] {
        if (view)
            view->reload();
    });
}

void MergeView::showMessage(const QString &message)
{
    message_->setText(message);
    message_->show();
}

void MergeView::saveCurrentFile()
{
    const ConflictVersions::File *file = currentFile();
    const MergeDocument *document = file ? openedDocument(file->path) : nullptr;
    if (!document || !document->isResolved())
        return;
    resolve(tr("Resolving %1").arg(file->path),
            [repository = conflicts_.repository, path = file->path, content = document->resultText()] {
                Integration::writeResolution(repository, path, content);
                return tr("%1 is resolved.").arg(path);
            });
}

void MergeView::resolveSimpleConflicts()
{
    if (!panel_)
        return;
    const int count = panel_->resolveSimpleConflicts();
    hint_->setText(count == 0 ? tr("No conflict left is simple: the sides changed the same words.")
                              : tr("%n simple conflict(s) resolved: check them, then save.", nullptr, count));
}

void MergeView::updateList()
{
    for (int row = 0; row < list_->count(); ++row) {
        const ConflictVersions::File &file = conflicts_.files[row];
        const QString state = MergeTexts::fileState(file, conflicts_.sides, openedDocument(file.path));
        list_->item(row)->setText(QFileInfo(file.path).fileName() + QLatin1Char('\n') + state);
    }
}

void MergeView::updateBar()
{
    const qsizetype files = conflicts_.files.size();
    const int total = int(mergedPaths_.size());
    const int ready = std::max(0, total - int(files));
    summary_->setText(tr("%1 of %2 files ready").arg(ready).arg(total));
    updateNavigator();
    const ConflictVersions::File *file = currentFile();
    const MergeDocument *document = file ? openedDocument(file->path) : nullptr;
    resolveSimple_->setEnabled(panel_ && document && !document->isResolved());
    updateAgentButton();
}

void MergeView::updateNavigator()
{
    if (panel_)
        navigator_->setConflicts(panel_->conflictStates(), panel_->currentConflict());
    else
        navigator_->setConflicts({}, -1);
}

void MergeView::onConflictResolved(int index, int next)
{
    hint_->setText(next < 0 ? tr("Conflict %1 resolved: none is left in this file, it can be saved.").arg(index + 1)
                            : tr("Conflict %1 resolved. On to conflict %2…").arg(index + 1).arg(next + 1));
}

const ConflictVersions::File *MergeView::currentFile() const
{
    const int row = list_->currentRow();
    return row >= 0 && row < conflicts_.files.size() ? &conflicts_.files[row] : nullptr;
}

MergeDocument *MergeView::documentOf(const ConflictVersions::File &file)
{
    if (!isText(file))
        return nullptr;
    Resolution &resolution = resolutions_[file.path];
    if (!resolution.document || resolution.file != file) {
        resolution.file = file;
        resolution.document = std::make_unique<MergeDocument>(
            ThreeWayMerge::merge(contentOf(file.base), contentOf(file.current), contentOf(file.incoming)));
    }
    return resolution.document.get();
}

const MergeDocument *MergeView::openedDocument(const QString &path) const
{
    const auto found = resolutions_.find(path);
    return found == resolutions_.end() ? nullptr : found->second.document.get();
}
