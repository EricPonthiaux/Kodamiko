#include "FilesPanel.h"

#include "ui/UiIcons.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

FilesPanel::FilesPanel(QWidget *parent)
    : QWidget(parent)
    , tree_(new FileTreeView(this))
{
    scope_ = new QComboBox(this);
    scope_->addItem(tr("Project"), static_cast<int>(FileTreeView::Scope::Project));
    scope_->addItem(tr("Changed Files"), static_cast<int>(FileTreeView::Scope::ChangedFiles));
    scope_->addItem(tr("Edited by Claude"), static_cast<int>(FileTreeView::Scope::EditedByClaude));
    scope_->setToolTip(tr("Files shown in the tree"));
    scope_->setFocusPolicy(Qt::NoFocus);
    connect(scope_, &QComboBox::currentIndexChanged, this,
            [this] { tree_->setScope(static_cast<FileTreeView::Scope>(scope_->currentData().toInt())); });

    // Icons only, all the same size (see setIconColor).
    const auto addButton = [this](const QString &toolTip) {
        auto *button = new QToolButton(this);
        button->setToolTip(toolTip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(16, 16));
        button->setFixedSize(26, 24);
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
    compactFolders_ = addOption(tr("Compact Middle Folders"), tr("“src/main/java” on one row when the folders between hold nothing else"));
    nestFiles_ = addOption(tr("File Nesting"), tr("Foo.h under Foo.cpp, package-lock.json under package.json…"));
    showIgnored_ = addOption(tr("Show Ignored Files"), tr("Files and folders ignored by git (build/, node_modules…), grayed"));
    menu->setToolTipsVisible(true);
    setTreeOptions(tree_->treeOptions());
    options->setMenu(menu);

    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(4, 2, 2, 2);
    toolbar->setSpacing(2);
    toolbar->addWidget(scope_, 1);
    toolbar->addWidget(select_);
    toolbar->addWidget(collapse);
    toolbar->addWidget(options);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(toolbar);
    layout->addWidget(tree_, 1);
}

void FilesPanel::setTreeOptions(const ProjectTreeModel::Options &options)
{
    for (QAction *action : {compactFolders_, nestFiles_, showIgnored_}) {
        const QSignalBlocker blocker(action);
        action->setChecked(action == compactFolders_ ? options.compactFolders
                           : action == nestFiles_    ? options.nestFiles
                                                     : options.showIgnored);
    }
    tree_->setTreeOptions(options);
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

void FilesPanel::setIconColor(const QColor &color)
{
    select_->setIcon(UiIcons::icon(QStringLiteral("locate"), color));
    collapse_->setIcon(UiIcons::icon(QStringLiteral("collapse-all"), color));
    options_->setIcon(UiIcons::icon(QStringLiteral("more"), color));
}
