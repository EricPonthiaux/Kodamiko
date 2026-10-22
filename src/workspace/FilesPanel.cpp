// SPDX-License-Identifier: GPL-3.0-only

#include "FilesPanel.h"

#include "ui/AppFonts.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"

#include <QActionGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>
#include <utility>

FilesPanel::FilesPanel(QWidget *parent)
    : QWidget(parent)
    , tree_(new FileTreeView(this))
{
    // The view selector: the view's name and a chevron, borderless, as
    // wide as its text.
    scope_ = new QToolButton(this);
    scope_->setObjectName(QStringLiteral("viewSelector"));
    scope_->setAutoRaise(true);
    scope_->setToolTip(tr("Files shown in the tree (Alt+Left, Alt+Right)"));
    scope_->setFocusPolicy(Qt::NoFocus);
    scope_->setPopupMode(QToolButton::InstantPopup);
    scope_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    AppFonts::setInterfaceFont(scope_, 1);
    auto *scopeMenu = new QMenu(scope_);
    scopes_ = new QActionGroup(this);
    scopes_->setExclusive(true);
    for (const auto &[name, scope] : {std::pair{tr("Project"), FileTreeView::Scope::Project},
                                      std::pair{tr("Changed Files"), FileTreeView::Scope::ChangedFiles},
                                      std::pair{QString(), FileTreeView::Scope::EditedByClaude}}) {
        QAction *action = scopeMenu->addAction(name);
        action->setCheckable(true);
        action->setData(static_cast<int>(scope));
        scopes_->addAction(action);
    }
    scopes_->actions().first()->setChecked(true);
    connect(scopes_, &QActionGroup::triggered, this, [this](QAction *action) {
        updateScopeText();
        tree_->setScope(static_cast<FileTreeView::Scope>(action->data().toInt()));
    });
    scope_->setMenu(scopeMenu);
    updateScopeText();
    // As IntelliJ: Alt+Left, Alt+Right go to the previous, next view.
    for (const auto &[key, step] : {std::pair{Qt::ALT | Qt::Key_Left, -1}, std::pair{Qt::ALT | Qt::Key_Right, 1}}) {
        auto *shortcut = new QShortcut(QKeySequence(key), this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        const int delta = step;
        connect(shortcut, &QShortcut::activated, this, [this, delta] {
            const int count = int(scopes_->actions().size());
            setScopeIndex((scopeIndex() + delta + count) % count);
        });
    }

    // Icons only, all the same size (see setIconColor).
    const auto addButton = [this](const QString &toolTip) {
        auto *button = new QToolButton(this);
        button->setToolTip(toolTip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        UiMetrics::setScaledIconSize(button);
        UiMetrics::setScaledSize(button, QSize(26, 24));
        return button;
    };
    select_ = addButton(tr("Select Opened File (Alt+F1)"));
    connect(select_, &QToolButton::clicked, this, &FilesPanel::selectOpenedFileRequested);
    collapse_ = addButton(tr("Collapse All"));
    QToolButton *collapse = collapse_;
    connect(collapse, &QToolButton::clicked, this, [this] {
        tree_->collapseAll();
        if (const QModelIndex root = tree_->model()->index(0, 0, tree_->rootIndex()); root.isValid())
            tree_->expand(root); // the working folder stays open
    });
    options_ = addButton(tr("Options"));
    QToolButton *options = options_;
    options->setPopupMode(QToolButton::InstantPopup);
    options->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0; }"));
    auto *menu = new QMenu(options);
    alwaysSelect_ = menu->addAction(tr("Always Select Opened File"));
    alwaysSelect_->setCheckable(true);
    connect(alwaysSelect_, &QAction::toggled, this, [this](bool checked) {
        emit alwaysSelectOpenedFileChanged(checked);
        if (checked)
            emit selectOpenedFileRequested();
    });
    filterWhileTyping_ = menu->addAction(tr("Filter While Typing"));
    filterWhileTyping_->setCheckable(true);
    filterWhileTyping_->setToolTip(tr("Letters typed in the tree hide the files whose names do not match"));
    connect(filterWhileTyping_, &QAction::toggled, this, [this](bool checked) {
        tree_->setFilterWhileTyping(checked);
        emit filterWhileTypingChanged(checked);
    });
    menu->addSeparator();
    const auto addOption = [this, menu](const QString &text, const QString &toolTip) {
        QAction *action = menu->addAction(text);
        action->setCheckable(true);
        action->setToolTip(toolTip);
        connect(action, &QAction::toggled, this, [this] {
            ProjectTreeModel::Options treeOptions;
            treeOptions.compactFolders = compactFolders_->isChecked();
            treeOptions.nestFiles = nestFiles_->isChecked();
            treeOptions.showIgnored = showIgnored_->isChecked();
            tree_->setTreeOptions(treeOptions);
            emit treeOptionsChanged(treeOptions);
        });
        return action;
    };
    compactFolders_ = addOption(tr("Compact Middle Folders"),
                                tr("“src/main/java” on one row when the folders between hold nothing else"));
    nestFiles_ = addOption(tr("File Nesting"), tr("Foo.h under Foo.cpp, package-lock.json under package.json…"));
    showIgnored_ =
        addOption(tr("Show Ignored Files"), tr("Files and folders ignored by git (build/, node_modules…), grayed"));
    menu->setToolTipsVisible(true);
    setTreeOptions(tree_->treeOptions());
    options->setMenu(menu);

    // The header: room around its controls, a line under it.
    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("viewHeader"));
    UiMetrics::setScaledHeight(header, UiMetrics::panelHeaderHeight);
    auto *toolbar = new QHBoxLayout(header);
    toolbar->setContentsMargins(6, 0, 6, 0);
    toolbar->setSpacing(2);
    toolbar->addWidget(scope_);
    toolbar->addStretch(1);
    toolbar->addWidget(select_);
    toolbar->addWidget(collapse);
    toolbar->addWidget(options);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(tree_, 1);
}

void FilesPanel::setTreeOptions(const ProjectTreeModel::Options &options)
{
    const std::array<std::pair<QAction *, bool>, 3> checks{{
        {compactFolders_, options.compactFolders},
        {nestFiles_, options.nestFiles},
        {showIgnored_, options.showIgnored},
    }};
    for (const auto &[action, checked] : checks) {
        const QSignalBlocker blocker(action);
        action->setChecked(checked);
    }
    tree_->setTreeOptions(options);
}

int FilesPanel::scopeIndex() const
{
    return int(scopes_->actions().indexOf(scopes_->checkedAction()));
}

void FilesPanel::setScopeIndex(int index)
{
    QAction *action = scopes_->actions().value(index);
    if (!action || action->isChecked())
        return;
    action->setChecked(true);
    updateScopeText();
    tree_->setScope(static_cast<FileTreeView::Scope>(action->data().toInt()));
}

void FilesPanel::setAgentName(const QString &name)
{
    tree_->setAgentName(name);
    for (QAction *action : scopes_->actions()) {
        if (static_cast<FileTreeView::Scope>(action->data().toInt()) == FileTreeView::Scope::EditedByClaude)
            action->setText(tr("Edited by %1").arg(name));
    }
    updateScopeText();
}

void FilesPanel::updateScopeText()
{
    scope_->setText(scopes_->checkedAction()->text() + QStringLiteral("  ▾"));
}

bool FilesPanel::alwaysSelectOpenedFile() const
{
    return alwaysSelect_->isChecked();
}

void FilesPanel::setAlwaysSelectOpenedFile(bool enabled)
{
    const QSignalBlocker blocker(alwaysSelect_);
    alwaysSelect_->setChecked(enabled);
}

void FilesPanel::setFilterWhileTyping(bool enabled)
{
    const QSignalBlocker blocker(filterWhileTyping_);
    filterWhileTyping_->setChecked(enabled);
    tree_->setFilterWhileTyping(enabled);
}

void FilesPanel::setIconColor(const QColor &color)
{
    select_->setIcon(UiIcons::icon(QStringLiteral("locate"), color));
    collapse_->setIcon(UiIcons::icon(QStringLiteral("collapse-all"), color));
    options_->setIcon(UiIcons::icon(QStringLiteral("more"), color));
}
