// SPDX-License-Identifier: GPL-3.0-only

#include "ClaudeConfigView.h"

#include "ui/AppFonts.h"
#include "ui/ContextMenus.h"
#include "ui/FileIcons.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"
#include "workspace/EntryPopups.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QScrollBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <array>
#include <utility>

using ClaudeConfig::Category;
using ClaudeConfig::Entry;
using ClaudeConfig::Scope;

namespace {

constexpr int refreshDelayMs = 400;

// Item data roles of the tree.
constexpr int entryRole = Qt::UserRole;
constexpr int categoryRole = Qt::UserRole + 1;
constexpr int effectiveRole = Qt::UserRole + 2;

// The key populate() keeps the "Effective Settings" item current with.
const QString effectiveKey = QStringLiteral("#effective");

constexpr std::array allCategories{Category::Memory,   Category::Rules,        Category::Skills,   Category::Agents,
                                   Category::Commands, Category::OutputStyles, Category::Settings, Category::Mcp};

ClaudeConfigView::Level levelOf(Scope scope)
{
    switch (scope) {
    case Scope::Managed:
    case Scope::User:
        return ClaudeConfigView::Level::User;
    case Scope::Parent:
    case Scope::Auto:
    case Scope::Project:
    case Scope::Subfolder:
    case Scope::Local:
    case Scope::Session:
        return ClaudeConfigView::Level::Project;
    }
    return ClaudeConfigView::Level::Project;
}

bool isEffectiveItem(const QTreeWidgetItem *item)
{
    return item && item->data(0, effectiveRole).toBool();
}

// "skill", "agent"... for the New menus and popups.
QString itemNoun(Category category)
{
    switch (category) {
    case Category::Skills:
        return ClaudeConfigView::tr("Skill");
    case Category::Agents:
        return ClaudeConfigView::tr("Agent");
    case Category::Commands:
        return ClaudeConfigView::tr("Command");
    case Category::Rules:
        return ClaudeConfigView::tr("Rule");
    default:
        return {};
    }
}

// A folder is removed with its content; a link, never followed.
bool removePermanently(const QFileInfo &info)
{
    if (info.isDir() && !info.isSymLink())
        return QDir(info.filePath()).removeRecursively();
    return QFile::remove(info.filePath());
}

bool createFile(const QString &path, const QByteArray &content, QString *error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = ClaudeConfigView::tr("Cannot create the folder “%1”")
                     .arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(content) != content.size()) {
        *error = ClaudeConfigView::tr("Cannot create “%1”: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}

} // namespace

ClaudeConfigView::ClaudeConfigView(QWidget *parent)
    : QWidget(parent)
    , tree_(new QTreeWidget(this))
{
    // The level selector: its name and a chevron, as the Files pane's view
    // selector.
    levelSelector_ = new QToolButton(this);
    levelSelector_->setObjectName(QStringLiteral("viewSelector"));
    levelSelector_->setAutoRaise(true);
    levelSelector_->setToolTip(tr("Claude configuration shown in the tree"));
    levelSelector_->setFocusPolicy(Qt::NoFocus);
    levelSelector_->setPopupMode(QToolButton::InstantPopup);
    levelSelector_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    AppFonts::setInterfaceFont(levelSelector_, 1);
    auto *levelMenu = new QMenu(levelSelector_);
    levels_ = new QActionGroup(this);
    levels_->setExclusive(true);
    for (const auto &[name, level] :
         {std::pair{tr("Project Configuration"), Level::Project}, std::pair{tr("User Configuration"), Level::User}}) {
        QAction *action = levelMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(level == level_);
        action->setData(static_cast<int>(level));
        levels_->addAction(action);
    }
    connect(levels_, &QActionGroup::triggered, this,
            [this](const QAction *action) { setLevel(static_cast<Level>(action->data().toInt())); });
    levelSelector_->setMenu(levelMenu);
    updateLevelText();

    // Icons only, all the same size (see setIconColor), as in the Files pane.
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
    const QString noIndicator = QStringLiteral("QToolButton::menu-indicator { image: none; width: 0; }");
    new_ = addButton(tr("New Skill, Agent, Command or Rule"));
    new_->setPopupMode(QToolButton::InstantPopup);
    new_->setStyleSheet(noIndicator);
    auto *newMenu = new QMenu(new_);
    for (Category category : allCategories) {
        if (ClaudeConfig::canCreate(category))
            addNewActions(newMenu->addMenu(itemNoun(category)), category);
    }
    new_->setMenu(newMenu);
    reload_ = addButton(tr("Reload"));
    connect(reload_, &QToolButton::clicked, this, &ClaudeConfigView::refresh);
    options_ = addButton(tr("Options"));
    options_->setPopupMode(QToolButton::InstantPopup);
    options_->setStyleSheet(noIndicator);
    auto *optionsMenu = new QMenu(options_);
    optionsMenu->addAction(tr("Effective Settings"), this, &ClaudeConfigView::effectiveSettingsRequested);
    optionsMenu->addAction(tr("Restart Claude Session"), this, &ClaudeConfigView::restartSessionRequested)
        ->setToolTip(tr("Claude reads some of its configuration only when it starts; the conversation goes on"));
    optionsMenu->addAction(tr("Open Claude Folder"), this,
                           [] { QDesktopServices::openUrl(QUrl::fromLocalFile(ClaudeConfig::configDir())); });
    optionsMenu->setToolTipsVisible(true);
    options_->setMenu(optionsMenu);

    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("viewHeader"));
    UiMetrics::setScaledHeight(header, UiMetrics::panelHeaderHeight);
    auto *toolbar = new QHBoxLayout(header);
    toolbar->setContentsMargins(10, 0, 6, 0);
    toolbar->setSpacing(2);
    toolbar->addWidget(levelSelector_);
    toolbar->addStretch(1);
    toolbar->addWidget(new_);
    toolbar->addWidget(reload_);
    toolbar->addWidget(options_);

    tree_->setColumnCount(2);
    tree_->header()->hide();
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->setUniformRowHeights(true);
    tree_->setAnimated(false);
    tree_->setExpandsOnDoubleClick(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::itemActivated, this, &ClaudeConfigView::activate);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &ClaudeConfigView::showContextMenu);
    auto *deleteAction = new QAction(tree_);
    deleteAction->setShortcut(QKeySequence::Delete);
    deleteAction->setShortcutContext(Qt::WidgetShortcut);
    connect(deleteAction, &QAction::triggered, this, [this] { confirmDelete(tree_->currentItem()); });
    tree_->addAction(deleteAction);
    connect(tree_, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem *item) { collapsed_.insert(item->data(0, categoryRole).toInt()); });
    connect(tree_, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) { collapsed_.remove(item->data(0, categoryRole).toInt()); });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(tree_, 1);

    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(refreshDelayMs);
    connect(&refreshTimer_, &QTimer::timeout, this, &ClaudeConfigView::refresh);
}

void ClaudeConfigView::setProjectFolder(const QString &folder)
{
    folder_ = folder;
    refresh();
}

void ClaudeConfigView::setLevel(Level level)
{
    for (QAction *action : levels_->actions()) {
        if (static_cast<Level>(action->data().toInt()) == level)
            action->setChecked(true);
    }
    if (level == level_)
        return;
    level_ = level;
    updateLevelText();
    populate();
}

void ClaudeConfigView::updateLevelText()
{
    levelSelector_->setText(levels_->checkedAction()->text() + QStringLiteral("  ▾"));
}

void ClaudeConfigView::setClaudeTouched(const QSet<QString> &paths)
{
    touched_ = paths;
    populate();
}

void ClaudeConfigView::setDarkIcons(bool dark)
{
    darkIcons_ = dark;
    populate();
}

void ClaudeConfigView::setIconColor(const QColor &color)
{
    new_->setIcon(UiIcons::icon(QStringLiteral("plus"), color));
    reload_->setIcon(UiIcons::icon(QStringLiteral("refresh"), color));
    options_->setIcon(UiIcons::icon(QStringLiteral("more"), color));
}

void ClaudeConfigView::refresh()
{
    refreshTimer_.stop();
    if (!isVisible()) {
        needsRefresh_ = true;
        return;
    }
    needsRefresh_ = false;
    entries_ = folder_.isEmpty() ? QList<Entry>() : ClaudeConfig::entries(folder_, {});
    populate();
    emit refreshed();
}

void ClaudeConfigView::scheduleRefresh()
{
    if (isVisible())
        refreshTimer_.start();
    else
        needsRefresh_ = true;
}

void ClaudeConfigView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (needsRefresh_)
        refresh();
}

const Entry *ClaudeConfigView::entryOf(const QTreeWidgetItem *item) const
{
    if (!item)
        return nullptr;
    const QVariant index = item->data(0, entryRole);
    return index.isValid() && index.toInt() < entries_.size() ? &entries_[index.toInt()] : nullptr;
}

void ClaudeConfigView::populate()
{
    const QTreeWidgetItem *previous = tree_->currentItem();
    QString current = entryOf(previous) ? entryOf(previous)->path : QString();
    if (isEffectiveItem(previous))
        current = effectiveKey;
    const int scroll = tree_->verticalScrollBar()->value();
    const QSignalBlocker blocker(tree_);
    tree_->clear();

    const QHash<int, QTreeWidgetItem *> categories = addCategoryItems();
    QTreeWidgetItem *currentItem = nullptr;
    if (!folder_.isEmpty() && level_ == Level::Project) {
        QTreeWidgetItem *effective = addEffectiveItem(categories.value(static_cast<int>(Category::Settings)));
        if (current == effectiveKey)
            currentItem = effective;
    }
    for (qsizetype i = 0; i < entries_.size(); ++i) {
        if (levelOf(entries_[i].scope) != level_)
            continue;
        QTreeWidgetItem *item = addEntryItem(i, categories.value(static_cast<int>(entries_[i].category)));
        if (entries_[i].path == current)
            currentItem = item;
    }
    for (QTreeWidgetItem *item : categories)
        summarizeCategory(item);
    if (currentItem)
        tree_->setCurrentItem(currentItem);
    tree_->verticalScrollBar()->setValue(scroll);
}

void ClaudeConfigView::setRightText(QTreeWidgetItem *item, const QString &text) const
{
    // Room after the scope and the counts, away from the edge.
    item->setText(1, text);
    item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    item->setSizeHint(1, QSize(tree_->fontMetrics().horizontalAdvance(text) + 18, 0));
}

QHash<int, QTreeWidgetItem *> ClaudeConfigView::addCategoryItems()
{
    const QColor muted = tree_->palette().color(QPalette::PlaceholderText);
    QHash<int, QTreeWidgetItem *> categories;
    for (Category category : allCategories) {
        auto *item = new QTreeWidgetItem(tree_);
        item->setText(0, ClaudeConfig::categoryTitle(category));
        item->setData(0, categoryRole, static_cast<int>(category));
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);
        item->setForeground(1, muted);
        categories.insert(static_cast<int>(category), item);
    }
    return categories;
}

QTreeWidgetItem *ClaudeConfigView::addEffectiveItem(QTreeWidgetItem *settings)
{
    auto *effective = new QTreeWidgetItem(settings);
    effective->setData(0, effectiveRole, true);
    effective->setText(0, tr("Effective Settings"));
    effective->setIcon(0, FileIcons::file(QStringLiteral("settings.json"), darkIcons_));
    setRightText(effective, tr("Merged"));
    effective->setForeground(1, tree_->palette().color(QPalette::PlaceholderText));
    effective->setToolTip(0, tr("The settings Claude applies, merged from every file, each value with its origin"));
    return effective;
}

QTreeWidgetItem *ClaudeConfigView::addEntryItem(qsizetype index, QTreeWidgetItem *category)
{
    const Entry &entry = entries_[index];
    const QPalette palette = tree_->palette();
    auto *item = new QTreeWidgetItem(category);
    item->setData(0, entryRole, int(index));
    item->setText(0, entry.name);
    setRightText(item, ClaudeConfig::scopeTitle(entry.scope));
    item->setForeground(1, palette.color(QPalette::PlaceholderText));
    const QIcon icon = FileIcons::file(QFileInfo(entry.path).fileName(), darkIcons_);
    item->setIcon(0, touched_.contains(entry.path) ? FileIcons::withClaudeDot(icon) : icon);

    QStringList tip{QDir::toNativeSeparators(entry.path), ClaudeConfig::scopeDescription(entry.scope)};
    if (!entry.description.isEmpty())
        tip.append(entry.description);
    if (!entry.exists) {
        QFont font = item->font(0);
        font.setItalic(true);
        item->setFont(0, font);
        item->setForeground(0, palette.color(QPalette::Disabled, QPalette::Text));
        tip.append(tr("Not created yet: double-click to create it"));
    } else if (entry.readOnly) {
        tip.append(tr("Read-only"));
    }
    item->setToolTip(0, tip.join(QLatin1Char('\n')));
    item->setToolTip(1, ClaudeConfig::scopeDescription(entry.scope));
    return item;
}

void ClaudeConfigView::summarizeCategory(QTreeWidgetItem *item)
{
    const int count = item->childCount();
    int created = 0;
    for (int i = 0; i < count; ++i) {
        const Entry *entry = entryOf(item->child(i));
        created += entry && entry->exists ? 1 : 0;
    }
    if (created > 0)
        setRightText(item, QString::number(created));
    if (count == 0)
        item->setForeground(0, tree_->palette().color(QPalette::Disabled, QPalette::Text));
    item->setExpanded(!collapsed_.contains(item->data(0, categoryRole).toInt()));
}

void ClaudeConfigView::activate(QTreeWidgetItem *item)
{
    if (isEffectiveItem(item)) {
        emit effectiveSettingsRequested();
        return;
    }
    const Entry *entry = entryOf(item);
    if (!entry)
        return;
    const QString path = entry->path;
    const bool readOnly = entry->readOnly;
    if (!entry->exists) {
        QString error;
        if (!createFile(path, ClaudeConfig::templateFor(*entry), &error)) {
            emit statusMessage(error);
            return;
        }
        refresh();
    }
    emit fileRequested(path, readOnly);
}

QString ClaudeConfigView::createNamed(Category category, Scope scope, const QString &name)
{
    if (!ClaudeConfig::isValidName(name))
        return tr("Lowercase letters, digits and hyphens only (“review-cpp”)");
    const QString path = ClaudeConfig::newEntryPath(category, scope, folder_, name);
    if (QFileInfo::exists(path))
        return tr("“%1” already exists").arg(QDir::toNativeSeparators(path));
    Entry entry;
    entry.category = category;
    entry.scope = scope;
    entry.name = name;
    QString error;
    if (!createFile(path, ClaudeConfig::templateFor(entry), &error))
        return error;
    collapsed_.remove(static_cast<int>(category));
    setLevel(levelOf(scope)); // shows the new item
    refresh();
    emit fileRequested(path, false);
    return {};
}

QString ClaudeConfigView::deletionTarget(const Entry &entry)
{
    if (!entry.exists || entry.readOnly)
        return {};
    return entry.category == Category::Skills ? QFileInfo(entry.path).absolutePath() : entry.path;
}

QString ClaudeConfigView::removeTarget(const QString &target, bool permanently)
{
    const QFileInfo info(target);
    if (!info.exists() && !info.isSymLink())
        return tr("“%1” no longer exists").arg(info.fileName());
    const bool removed = permanently ? removePermanently(info) : QFile::moveToTrash(target);
    if (!removed)
        return permanently ? tr("Cannot delete “%1”").arg(info.fileName())
                           : tr("“%1” cannot be moved to the trash").arg(info.fileName());
    emit entryDeleted(target);
    refresh();
    return {};
}

void ClaudeConfigView::confirmDelete(QTreeWidgetItem *item)
{
    const Entry *entry = entryOf(item);
    const QString target = entry ? deletionTarget(*entry) : QString();
    if (target.isEmpty())
        return;
    const bool isFolder = QFileInfo(target).isDir();
    QString message = isFolder ? tr("Delete the skill “%1” and its folder?").arg(entry->name)
                               : tr("Delete “%1”?").arg(QFileInfo(target).fileName());
    message += QLatin1Char('\n') + tr("It will be moved to the trash.");
    if (hasUnsavedChanges_ && hasUnsavedChanges_(target))
        message += QLatin1Char(' ') + tr("Unsaved changes in the editor will be lost.");
    auto *popup = new ConfirmPopup(
        {isFolder ? tr("Delete Skill") : tr("Delete File"),
         QDir::toNativeSeparators(QFileInfo(target).absolutePath() + QLatin1Char('/')), message, tr("Delete")},
        [this, target, permanently = false](ConfirmPopup *confirm) mutable {
            const QString error = removeTarget(target, permanently);
            if (error.isEmpty())
                return true;
            if (permanently) {
                confirm->showError(error);
                return false;
            }
            permanently = true;
            confirm->setMessage(tr("“%1” cannot be moved to the trash. Delete it permanently? This cannot be undone.")
                                    .arg(QFileInfo(target).fileName()),
                                tr("Delete Permanently"));
            return false;
        },
        this);
    popup->popup();
}

void ClaudeConfigView::addNewActions(QMenu *menu, Category category)
{
    menu->addAction(tr("In This Project…"), this, [this, category] { askName(category, Scope::Project); });
    menu->addAction(tr("For All Projects…"), this, [this, category] { askName(category, Scope::User); });
}

void ClaudeConfigView::askName(Category category, Scope scope)
{
    if (folder_.isEmpty())
        return;
    const QString path = ClaudeConfig::newEntryPath(category, scope, folder_, QStringLiteral("x"));
    // The folder the item goes in: skills/, agents/...
    const QString location = category == Category::Skills ? QFileInfo(QFileInfo(path).absolutePath()).absolutePath()
                                                          : QFileInfo(path).absolutePath();
    auto *popup = new NamePopup(
        tr("New %1").arg(itemNoun(category)), QDir::toNativeSeparators(location + QLatin1Char('/')),
        [this, category, scope](const QString &name) { return createNamed(category, scope, name); }, this);
    popup->popup();
}

void ClaudeConfigView::showContextMenu(const QPoint &position)
{
    QTreeWidgetItem *item = tree_->itemAt(position);
    if (!item)
        return;
    QMenu menu;
    const Entry *entry = entryOf(item);
    if (item->data(0, effectiveRole).toBool()) {
        menu.addAction(tr("&Open"), this, &ClaudeConfigView::effectiveSettingsRequested);
        ContextMenus::exec(menu, *tree_, tree_->viewport()->mapToGlobal(position));
        return;
    }
    if (!entry) {
        const auto category = static_cast<Category>(item->data(0, categoryRole).toInt());
        if (!ClaudeConfig::canCreate(category))
            return;
        addNewActions(menu.addMenu(tr("New %1").arg(itemNoun(category))), category);
        ContextMenus::exec(menu, *tree_, tree_->viewport()->mapToGlobal(position));
        return;
    }

    const QString path = entry->path;
    menu.addAction(entry->exists ? tr("&Open") : tr("&Create"), this, [this, item] { activate(item); });
    if (entry->exists) {
        menu.addAction(tr("Send to &Claude"), this, [this, path] { emit sendToClaudeRequested(path); });
        menu.addSeparator();
        menu.addAction(tr("Copy &Path"), this,
                       [path] { QApplication::clipboard()->setText(QDir::toNativeSeparators(path)); });
        menu.addAction(tr("Open in &File Manager"), this,
                       [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())); });
        if (!deletionTarget(*entry).isEmpty()) {
            menu.addSeparator();
            menu.addAction(tr("&Delete…"), QKeySequence::Delete, this, [this, item] { confirmDelete(item); });
        }
    }
    ContextMenus::exec(menu, *tree_, tree_->viewport()->mapToGlobal(position));
}
