// SPDX-License-Identifier: GPL-3.0-only

#include "Keymap.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>

namespace Keymap {
namespace {

// Property of a command's action holding its default shortcuts.
constexpr const char *defaultProperty = "keymapDefault";
// Property of an action or a menu naming it in command ids (setStableName()).
constexpr const char *stableNameProperty = "keymapStableName";

// A menu text without its mnemonic and its ellipsis.
QString plainText(QString text)
{
    text.remove(QLatin1Char('&'));
    if (text.endsWith(QChar(0x2026))) // "…"
        text.chop(1);
    return text.trimmed();
}

QString idName(const QAction *action)
{
    const QString stableName = action->property(stableNameProperty).toString();
    return plainText(stableName.isEmpty() ? action->text() : stableName);
}

QList<QKeySequence> parse(const QString &text)
{
    QList<QKeySequence> shortcuts = QKeySequence::listFromString(text, QKeySequence::PortableText);
    shortcuts.removeAll(QKeySequence()); // "" reads as one empty shortcut
    return shortcuts;
}

// The path of a menu as the user reads it, and as command ids name it.
struct MenuPath {
    QStringList shown;
    QStringList id;
};

void collect(const QMenu *menu, const MenuPath &path, QList<Command> &result)
{
    for (QAction *action : menu->actions()) {
        if (action->isSeparator() || plainText(action->text()).isEmpty())
            continue;
        const MenuPath actionPath{path.shown + QStringList{plainText(action->text())},
                                  path.id + QStringList{idName(action)}};
        if (const QMenu *submenu = action->menu()) {
            collect(submenu, actionPath, result);
            continue;
        }
        if (!action->property(defaultProperty).isValid())
            action->setProperty(defaultProperty,
                                QKeySequence::listToString(action->shortcuts(), QKeySequence::PortableText));
        result.append({actionPath.id.join(QLatin1Char('/')), actionPath.shown.join(QStringLiteral(" ▸ ")), action,
                       parse(action->property(defaultProperty).toString())});
    }
}

} // namespace

void setStableName(QAction *action, const QString &name)
{
    action->setProperty(stableNameProperty, name);
}

QList<Command> commands(const QMenuBar *menuBar)
{
    QList<Command> result;
    for (const QAction *action : menuBar->actions()) {
        if (const QMenu *menu = action->menu())
            collect(menu, {.shown = {plainText(action->text())}, .id = {idName(action)}}, result);
    }
    return result;
}

Overrides loadOverrides()
{
    Overrides overrides;
    const QVariantMap saved = QSettings().value(QStringLiteral("keymap")).toMap();
    for (auto it = saved.begin(); it != saved.end(); ++it)
        overrides.insert(it.key(), parse(it.value().toString()));
    return overrides;
}

void saveOverrides(const Overrides &overrides)
{
    QVariantMap saved;
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        saved.insert(it.key(), QKeySequence::listToString(it.value(), QKeySequence::PortableText));
    QSettings settings;
    if (saved.isEmpty())
        settings.remove(QStringLiteral("keymap"));
    else
        settings.setValue(QStringLiteral("keymap"), saved);
}

QList<QKeySequence> shortcutsOf(const Command &command, const Overrides &overrides)
{
    return overrides.value(command.id, command.defaultShortcuts);
}

void apply(const QList<Command> &commands, const Overrides &overrides)
{
    for (const Command &command : commands)
        command.action->setShortcuts(shortcutsOf(command, overrides));
}

} // namespace Keymap
