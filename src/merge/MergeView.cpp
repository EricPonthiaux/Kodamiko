// SPDX-License-Identifier: GPL-3.0-only

#include "MergeView.h"

#include "MergeChoicePanel.h"
#include "MergeTextPanel.h"
#include "MergeTexts.h"
#include "git/GitCommands.h"
#include "git/ThreeWayMerge.h"
#include "ui/AppFonts.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr int listWidthPx = 260;
constexpr int barMarginPx = 10;
constexpr int barSpacingPx = 8;

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
    , list_(new QListWidget(this))
    , message_(new QLabel(this))
{
    list_->setFixedWidth(listWidthPx);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setWordWrap(true);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(list_, &QListWidget::currentRowChanged, this, &MergeView::showFile);
    message_->setObjectName(QStringLiteral("pageDescription"));
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);
    connect(commands_, &GitCommands::busyChanged, this, &MergeView::updateFooter);

    auto *fileArea = new QWidget(this);
    fileArea_ = new QVBoxLayout(fileArea);
    fileArea_->setContentsMargins(0, 0, 0, 0);
    fileArea_->addWidget(message_);

    auto *body = new QHBoxLayout;
    body->setSpacing(0);
    body->addWidget(list_);
    body->addWidget(fileArea, 1);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createBar());
    layout->addLayout(body, 1);
    layout->addWidget(createFooter());
    showMessage(tr("Reading the conflicts…"));
    reload();
}

MergeView::~MergeView() = default;

void MergeView::reload()
{
    pool_.start([this, folder = folder_] {
        Conflicts conflicts;
        try {
            const std::optional<RepositoryInfo> repository = RepositoryInfo::read(folder);
            if (repository) {
                conflicts.repository = *repository;
                conflicts.files = ConflictVersions::read(*repository);
                conflicts.sides = Integration::conflictSides(*repository);
            } else {
                conflicts.error = tr("This folder is not in a git repository.");
            }
        } catch (const GitError &error) {
            conflicts.error = error.message();
        }
        QMetaObject::invokeMethod(this, [this, conflicts] { showConflicts(conflicts); }, Qt::QueuedConnection);
    });
}

void MergeView::applyTheme(const Theme &theme)
{
    theme_ = theme;
    if (panel_)
        panel_->applyTheme(theme);
    if (choices_)
        choices_->applyTheme(theme);
}

QWidget *MergeView::createBar()
{
    auto *bar = new QWidget(this);
    title_ = new QLabel(bar);
    AppFonts::setInterfaceFont(title_, 1.05, QFont::DemiBold);
    title_->setTextFormat(Qt::PlainText);
    summary_ = new QLabel(bar);
    summary_->setObjectName(QStringLiteral("pageDescription"));
    previous_ = new QPushButton(tr("Previous Conflict"), bar);
    previous_->setToolTip(tr("Shift+F7"));
    next_ = new QPushButton(tr("Next Conflict"), bar);
    next_->setToolTip(tr("F7"));
    resolveSimple_ = new QPushButton(tr("Resolve Simple Conflicts"), bar);
    resolveSimple_->setToolTip(tr("Resolve the conflicts where the sides differ only by whitespace or changed "
                                  "different words"));
    connect(resolveSimple_, &QPushButton::clicked, this, &MergeView::resolveSimpleConflicts);
    connect(previous_, &QPushButton::clicked, this, [this] {
        if (panel_)
            panel_->goToConflict(MergeTextPanel::Direction::Previous);
    });
    connect(next_, &QPushButton::clicked, this, [this] {
        if (panel_)
            panel_->goToConflict(MergeTextPanel::Direction::Next);
    });
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(barMarginPx, barMarginPx, barMarginPx, barMarginPx);
    layout->setSpacing(barSpacingPx);
    layout->addWidget(title_);
    layout->addWidget(summary_);
    layout->addStretch();
    layout->addWidget(resolveSimple_);
    layout->addWidget(previous_);
    layout->addWidget(next_);
    return bar;
}

QWidget *MergeView::createFooter()
{
    auto *footer = new QWidget(this);
    hint_ = new QLabel(footer);
    hint_->setObjectName(QStringLiteral("pageDescription"));
    save_ = new QPushButton(tr("Save and Mark Resolved"), footer);
    save_->setToolTip(tr("Write the result to the file and mark it resolved in git"));
    abort_ = new QPushButton(footer);
    continue_ = new QPushButton(footer);
    continue_->setDefault(true);
    close_ = new QPushButton(tr("Close"), footer);
    connect(save_, &QPushButton::clicked, this, &MergeView::saveCurrentFile);
    connect(abort_, &QPushButton::clicked, this, &MergeView::abortRequested);
    connect(continue_, &QPushButton::clicked, this, &MergeView::continueRequested);
    connect(close_, &QPushButton::clicked, this, &MergeView::closeRequested);
    auto *layout = new QHBoxLayout(footer);
    layout->setContentsMargins(barMarginPx, barMarginPx, barMarginPx, barMarginPx);
    layout->setSpacing(barSpacingPx);
    layout->addWidget(hint_, 1);
    layout->addWidget(save_);
    layout->addWidget(abort_);
    layout->addWidget(continue_);
    layout->addWidget(close_);
    return footer;
}

void MergeView::showConflicts(const Conflicts &conflicts)
{
    // Reloads follow every change of the repository: most change nothing.
    if (hasRead_ && conflicts == conflicts_)
        return;
    hasRead_ = true;
    conflicts_ = conflicts;
    std::erase_if(resolutions_, [&conflicts](const auto &entry) {
        return std::ranges::find(conflicts.files, entry.second.file) == conflicts.files.end();
    });
    title_->setText(MergeTexts::title(conflicts.repository.operation, conflicts.sides));
    const QString selected = list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString() : QString();
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
    if (!conflicts.error.isEmpty()) {
        showFile(-1);
        showMessage(conflicts.error);
    } else if (conflicts_.files.isEmpty()) {
        showFile(-1);
        showMessage(conflicts.repository.operation.isEmpty() ? tr("No conflict is left.")
                                                             : tr("Every conflict is resolved."));
    } else {
        const auto found = std::ranges::find(conflicts_.files, selected, &ConflictVersions::File::path);
        // From no row after the clear: showFile() follows.
        list_->setCurrentRow(found == conflicts_.files.end() ? 0 : int(found - conflicts_.files.begin()));
    }
    updateBar();
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
    MergeDocument *document = documentOf(file);
    message_->hide();
    if (!document) {
        showChoices(file);
        updateBar();
        updateFooter();
        return;
    }
    panel_ = new MergeTextPanel(theme_, {file.path, document, conflicts_.sides}, this);
    connect(panel_, &MergeTextPanel::documentChanged, this, [this] {
        updateList();
        updateBar();
        updateFooter();
    });
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
    int unresolved = 0;
    for (const auto &[path, resolution] : resolutions_)
        unresolved += resolution.document->unresolvedCount();
    const qsizetype files = conflicts_.files.size();
    QString summary = files == 0 ? QString() : tr("%n file(s) in conflict", nullptr, int(files));
    if (!resolutions_.empty())
        summary += QStringLiteral(" · ") + tr("%n conflict(s) left in the files opened", nullptr, unresolved);
    summary_->setText(summary);
    previous_->setEnabled(panel_);
    next_->setEnabled(panel_);
    const ConflictVersions::File *file = currentFile();
    const MergeDocument *document = file ? openedDocument(file->path) : nullptr;
    resolveSimple_->setEnabled(panel_ && document && !document->isResolved());
}

void MergeView::updateFooter()
{
    const QString &operation = conflicts_.repository.operation;
    const bool hasOperation = Integration::canContinue(operation);
    const bool isDone = hasRead_ && conflicts_.error.isEmpty() && conflicts_.files.isEmpty();
    const ConflictVersions::File *file = currentFile();
    const MergeDocument *document = file ? openedDocument(file->path) : nullptr;
    const bool canSave = document && document->isResolved() && !commands_->isBusy();
    save_->setVisible(!isDone && document);
    save_->setEnabled(canSave);
    abort_->setText(tr("Abort %1").arg(operation));
    abort_->setVisible(hasOperation);
    continue_->setText(tr("Continue %1").arg(operation));
    continue_->setVisible(hasOperation);
    continue_->setEnabled(isDone && !commands_->isBusy());
    close_->setVisible(isDone && !hasOperation);
    if (isDone)
        hint_->setText(hasOperation ? tr("Every file is resolved: the %1 can go on.").arg(operation) : QString());
    else if (!document)
        hint_->setText(tr("A choice resolves the file at once; Kodamiko takes a snapshot first."));
    else if (!document->isResolved())
        hint_->setText(tr("Take a side, both, or type the result for each conflict, then save."));
    else
        hint_->setText(tr("Saving writes the result and marks the file resolved."));
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
