#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QString>

class QAction;
class QMenuBar;

// Keyboard shortcuts of the menu commands, which the user can change
// (Preferences ▸ Keymap). Only the changes are saved: a command the user
// left alone follows the shortcut of new versions.
namespace Keymap {

struct Command {
    QString id;    // menu path, "Edit/Format Document"
    QString title; // as shown, "Edit ▸ Format Document"
    QAction *action;
    QList<QKeySequence> defaultShortcuts;
};

// Shortcuts by command id; an empty list removes the command's shortcut.
using Overrides = QHash<QString, QList<QKeySequence>>;

// Commands of the menus of `menuBar`, submenus included, in menu order. The
// first call records each command's current shortcuts as its defaults.
QList<Command> commands(const QMenuBar *menuBar);

Overrides loadOverrides();
void saveOverrides(const Overrides &overrides);

// Shortcuts of `command` with these overrides.
QList<QKeySequence> shortcutsOf(const Command &command, const Overrides &overrides);

// Gives each command its shortcuts: the override, or else the default.
void apply(const QList<Command> &commands, const Overrides &overrides);

// Every shortcut the commands use (see EditorWidget::setReservedKeys).
QList<QKeySequence> shortcutsInUse(const QList<Command> &commands);

} // namespace Keymap
