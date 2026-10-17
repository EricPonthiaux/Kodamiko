#pragma once

#include "ClaudeConfig.h"

#include <QSet>
#include <QTimer>
#include <QWidget>

class QMenu;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// Claude pane of the side panel: everything Claude Code reads for the
// project (memory, rules, skills, agents, commands, settings, MCP servers),
// grouped by category, each file with its scope (User, Project, Local...).
// A double-click opens the file in the editor; a usual file not created
// yet (CLAUDE.local.md...) is created first. New makes a skill, an agent, a
// command or a rule, in the project or for every project.
//
// The list is reread when the pane shows, on refresh() and after Claude's
// edits; only while the pane is visible.
class ClaudeConfigView : public QWidget {
    Q_OBJECT

public:
    explicit ClaudeConfigView(QWidget *parent = nullptr);

    void setProjectFolder(const QString &folder);
    // The --settings file of Duet's Claude session, listed read-only.
    void setSessionSettings(const QString &path);
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

signals:
    void fileRequested(const QString &path, bool readOnly);
    void sendToClaudeRequested(const QString &path);
    void restartSessionRequested();
    void statusMessage(const QString &message);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void populate();
    void showContextMenu(const QPoint &position);
    void addNewActions(QMenu *menu, ClaudeConfig::Category category);
    void askName(ClaudeConfig::Category category, ClaudeConfig::Scope scope);

    QString folder_;
    QString sessionSettings_;
    QSet<QString> touched_;
    bool darkIcons_ = true;
    QList<ClaudeConfig::Entry> entries_;
    // Categories the user collapsed, kept from one reading to the next.
    QSet<int> collapsed_;
    QTimer refreshTimer_;
    bool needsRefresh_ = false;

    QTreeWidget *tree_;
    QToolButton *new_;
    QToolButton *reload_;
    QToolButton *options_;
};
