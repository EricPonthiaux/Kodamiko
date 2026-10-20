// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QIcon>

// Line icons of the interface (toolbars, title bar, status bar), drawn in
// the theme's colors: "search", "find-in-files", "palette", "branch",
// "plus", "minus", "discard", "refresh", "terminal", "code", "diff",
// "history", "files", "outline", "claude", "locate", "appearance",
// "keyboard", "settings", "collapse-all", "filter", "more", "run", "stop", "build",
// "debug", "test", "problems", "check", "document", "split", "preview", "arrange",
// "fit", "export", "grid", "side-panel".
namespace UiIcons {

QIcon icon(const QString &name, const QColor &color);

} // namespace UiIcons
