#include "FilesPanel.h"

#include "UiIcons.h"

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

    const auto addButton = [this](const QString &text, const QString &toolTip) {
        auto *button = new QToolButton(this);
        button->setText(text);
        button->setToolTip(toolTip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };
    select_ = addButton(QString(), tr("Select Opened File (Alt+F1)"));
    select_->setIconSize(QSize(16, 16));
    connect(select_, &QToolButton::clicked, this, &FilesPanel::selectOpenedFileRequested);
    QToolButton *collapse = addButton(QStringLiteral("⊟"), tr("Collapse All"));
    connect(collapse, &QToolButton::clicked, this, [this] {
        tree_->collapseAll();
        if (const QModelIndex root = tree_->model()->index(0, 0, tree_->rootIndex()); root.isValid())
            tree_->expand(root); // the working folder stays open
    });
    QToolButton *options = addButton(QStringLiteral("⋮"), tr("Options"));
    options->setPopupMode(QToolButton::InstantPopup);
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
}
