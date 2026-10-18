#pragma once

#include "RunConfiguration.h"
#include "ui/Theme.h"

class QMenu;
class QWidgetAction;

// Entries of the run configuration menus, drawn like an IDE's run chooser
// rather than as plain menu items.
namespace RunConfigurationMenu {

// A configuration on two lines: its kind's icon, its name, its command
// below in muted text; on the right a green dot while it runs, a check
// mark when it is the selected one. Triggering the action chooses it.
struct RowState {
    bool selected = false;
    bool running = false;
};

QWidgetAction *addConfiguration(QMenu *menu, const RunConfiguration &configuration, RowState state, const Theme &theme);

// Small uppercase heading over a group of configurations.
void addHeading(QMenu *menu, const QString &text, const Theme &theme);

// Muted line telling why the menu is empty, or what went wrong.
void addNote(QMenu *menu, const QString &text, const Theme &theme);

} // namespace RunConfigurationMenu
