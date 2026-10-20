#pragma once

#include "ClaudeConfig.h"

#include <QHash>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include <functional>

class QActionGroup;
class QMenu;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// Claude pane of the side panel: everything Claude Code reads for the
// project (memory, rules, skills, agents, commands, settings, MCP servers),
// grouped by category, each file with its scope (User, Project, Local...).
// A selector in the header shows either the project's files (the default)
// or the user's, for every project (User, Managed).
// A double-click opens the file in the editor; a usual file not created
// yet (CLAUDE.local.md...) is created first. New makes a skill, an agent, a
// command or a rule, in the project or for every project; Delete moves an
// item to the trash (a skill with its folder). Under Settings,
// Effective Settings shows them merged (see EffectiveSettings).
//
// Duet's own hooks (its --settings file) are not listed: Duet needs them.
//
// The list is reread when the pane shows, on refresh() and after Claude's
// edits; only while the pane is visible.
class ClaudeConfigView : public QWidget {
    Q_OBJECT

public:
    // The files the tree shows.
    enum class Level {
        Project, // the project's, its parent folders' and Claude's memory of it
        User,    // ~/.claude and the administrator's, for every project
    };

    explicit ClaudeConfigView(QWidget *parent = nullptr);

    Level level() const { return level_; }

    void setLevel(Level level);

    void setProjectFolder(const QString &folder);
    // Files Claude wrote during the last prompt: an orange dot.
    void setClaudeTouched(const QSet<QString> &paths);
    void setDarkIcons(bool dark);
    // Color of the toolbar's line icons, from the theme.
    void setIconColor(const QColor &color);
    // Rereads the configuration, at once if the pane is visible.
    void refresh();
    // The same, a moment later: coalesces a burst of edits.
    void scheduleRefresh();

    QTreeWidget *tree() const { return tree_; }

    // The entry shown by `item`, or nullptr for a category.
    const ClaudeConfig::Entry *entryOf(const QTreeWidgetItem *item) const;
    // Opens `item`'s file, creating it first if needed (as a double-click).
    void activate(QTreeWidgetItem *item);
    // Makes a new item of `category` named `name` in `scope` (Project or
    // User), then opens it: an error message, or an empty string.
    QString createNamed(ClaudeConfig::Category category, ClaudeConfig::Scope scope, const QString &name);
    // What deleting `entry` removes: its file, or a skill's folder; empty
    // if it cannot be deleted (not created yet, read-only).
    static QString deletionTarget(const ClaudeConfig::Entry &entry);
    // Asks to confirm, then deletes `item`'s file (see deletionTarget).
    void confirmDelete(QTreeWidgetItem *item);
    // Moves `target` to the trash, or deletes it for good: an error
    // message, or an empty string.
    QString removeTarget(const QString &target, bool permanently);

    void setUnsavedChangesCheck(std::function<bool(const QString &)> check) { hasUnsavedChanges_ = std::move(check); }

signals:
    void fileRequested(const QString &path, bool readOnly);
    void sendToClaudeRequested(const QString &path);
    void restartSessionRequested();
    // `path`, a file or a skill's folder, was deleted.
    void entryDeleted(const QString &path);
    void effectiveSettingsRequested();
    // The configuration was reread.
    void refreshed();
    void statusMessage(const QString &message);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void populate();
    void updateLevelText();
    void setRightText(QTreeWidgetItem *item, const QString &text) const;
    QHash<int, QTreeWidgetItem *> addCategoryItems();
    QTreeWidgetItem *addEffectiveItem(QTreeWidgetItem *settings);
    QTreeWidgetItem *addEntryItem(qsizetype index, QTreeWidgetItem *category);
    void summarizeCategory(QTreeWidgetItem *item);
    void showContextMenu(const QPoint &position);
    void addNewActions(QMenu *menu, ClaudeConfig::Category category);
    void askName(ClaudeConfig::Category category, ClaudeConfig::Scope scope);

    QString folder_;
    Level level_ = Level::Project;
    QSet<QString> touched_;
    bool darkIcons_ = true;
    QList<ClaudeConfig::Entry> entries_;
    // Categories the user collapsed, kept from one reading to the next.
    QSet<int> collapsed_;
    QTimer refreshTimer_;
    bool needsRefresh_ = false;
    std::function<bool(const QString &)> hasUnsavedChanges_;

    QTreeWidget *tree_;
    QToolButton *levelSelector_;
    QActionGroup *levels_;
    QToolButton *new_;
    QToolButton *reload_;
    QToolButton *options_;
};
