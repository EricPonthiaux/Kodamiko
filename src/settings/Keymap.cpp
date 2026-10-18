#include "Keymap.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>

namespace Keymap {
namespace {

// Property of a command's action holding its default shortcuts.
constexpr char defaultProperty[] = "keymapDefault";

QString plainText(const QAction *action)
{
    QString text = action->text();
    text.remove(QLatin1Char('&'));
    if (text.endsWith(QChar(0x2026))) // "…"
        text.chop(1);
    return text.trimmed();
}

QList<QKeySequence> parse(const QString &text)
{
    QList<QKeySequence> shortcuts = QKeySequence::listFromString(text, QKeySequence::PortableText);
    shortcuts.removeAll(QKeySequence()); // "" reads as one empty shortcut
    return shortcuts;
}

void collect(const QMenu *menu, const QStringList &path, QList<Command> &result)
{
    for (QAction *action : menu->actions()) {
        if (action->isSeparator() || plainText(action).isEmpty())
            continue;
        const QStringList actionPath = path + QStringList{plainText(action)};
        if (const QMenu *submenu = action->menu()) {
            collect(submenu, actionPath, result);
            continue;
        }
        if (!action->property(defaultProperty).isValid())
            action->setProperty(defaultProperty,
                                QKeySequence::listToString(action->shortcuts(), QKeySequence::PortableText));
        result.append({actionPath.join(QLatin1Char('/')), actionPath.join(QStringLiteral(" ▸ ")), action,
                       parse(action->property(defaultProperty).toString())});
    }
}

} // namespace

QList<Command> commands(const QMenuBar *menuBar)
{
    QList<Command> result;
    for (const QAction *action : menuBar->actions()) {
        if (const QMenu *menu = action->menu())
            collect(menu, {plainText(action)}, result);
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

QList<QKeySequence> shortcutsInUse(const QList<Command> &commands)
{
    QList<QKeySequence> result;
    for (const Command &command : commands)
        result.append(command.action->shortcuts());
    return result;
}

} // namespace Keymap
