// SPDX-License-Identifier: GPL-3.0-only

// Linked into the real duet executable by capture.sh, never into a release:
// once the application exists, it reads commands from $DUET_SHOTS/command
// and runs them on the windows, so that a script can set a scene up and
// grab it. One command per line:
//   mode <Terminal|Edit|Review|Diff|History>   the tab of the mode bar
//   detach | attach                             the Terminals window
//   place <window> <x> <y> <width> <height>     main | terminals
//   action <text>                               a menu action, without '&'
//   focus <class name>                          the widget that gets the keys
//   type <text> | key <sequence>                to that widget, or the focused one
//   click <text or object name>                 a button of the main window
//   press <window> <x> <y>                      a left click at that point
//   grab <name>                                 every window, <name>-<window>.png
//   screen <name>                               the whole screen, <name>.png
//   dump                                        the widget tree, widgets.txt
//   quit
// It writes $DUET_SHOTS/done once a file of commands has run.

#include "ui/DetachablePage.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMainWindow>
#include <QPointer>
#include <QScreen>
#include <QTabBar>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <QWindow>

#include <unistd.h>

namespace {

constexpr int pollMs = 200;

// Offscreen, no window is ever active: the keys go to the widget named by
// the last focus command.
QPointer<QWidget> keyTarget;

QString shotsFolder()
{
    return qEnvironmentVariable("DUET_SHOTS");
}

QMainWindow *mainWindow()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *window = qobject_cast<QMainWindow *>(widget); window && window->isVisible())
            return window;
    }
    return nullptr;
}

QWidget *terminalsWindow()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->isVisible() && widget->windowTitle().startsWith(QStringLiteral("Terminals")))
            return widget;
    }
    return nullptr;
}

QWidget *windowNamed(const QString &name)
{
    return name == QStringLiteral("terminals") ? terminalsWindow() : mainWindow();
}

void showMode(QMainWindow *window, const QString &mode)
{
    auto *tabs = window->findChild<QTabBar *>(QStringLiteral("modeTabs"));
    for (int i = 0; tabs && i < tabs->count(); ++i) {
        if (tabs->tabText(i) == mode)
            tabs->setCurrentIndex(i);
    }
}

void triggerAction(QMainWindow *window, const QString &text)
{
    for (QAction *action : window->findChildren<QAction *>()) {
        if (action->text().remove(QLatin1Char('&')) == text) {
            action->trigger();
            return;
        }
    }
    qWarning("SceneDriver: no action \"%s\"", qPrintable(text));
}

void clickButton(QMainWindow *window, const QString &name)
{
    for (QAbstractButton *button : window->findChildren<QAbstractButton *>()) {
        const bool isNamed = button->objectName() == name || button->text().remove(QLatin1Char('&')) == name;
        if (isNamed && button->isVisible()) {
            button->click();
            return;
        }
    }
    qWarning("SceneDriver: no button \"%s\"", qPrintable(name));
}

// Through the window system, as a user would: QApplication::mouseButtons()
// then holds the button, which a sent event does not.
void pressAt(const QStringList &words)
{
    QWidget *window = windowNamed(words.value(1));
    if (!window || words.size() < 4)
        return;
    const QPoint point(words[2].toInt(), words[3].toInt());
    QWidget *target = window->childAt(point);
    if (!target)
        target = window;
    QTest::mouseClick(target, Qt::LeftButton, {}, target->mapFrom(window, point));
}

void sendKey(int key, Qt::KeyboardModifiers modifiers, const QString &text)
{
    QWidget *target = keyTarget ? keyTarget.data() : QApplication::focusWidget();
    if (!target)
        return;
    QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
    QApplication::sendEvent(target, &press);
    QApplication::sendEvent(target, &release);
}

void typeText(const QString &text)
{
    for (const QChar character : text)
        sendKey(character.unicode(), Qt::NoModifier, QString(character));
}

void pressKeys(const QString &sequence)
{
    const QKeyCombination combination = QKeySequence::fromString(sequence)[0];
    const bool isReturn = combination.key() == Qt::Key_Return;
    sendKey(combination.key(), combination.keyboardModifiers(), isReturn ? QStringLiteral("\r") : QString());
}

void focusWidgetOfClass(const QString &className)
{
    for (QWidget *window : QApplication::topLevelWidgets()) {
        for (QWidget *widget : window->findChildren<QWidget *>()) {
            if (widget->isVisible() && QLatin1String(widget->metaObject()->className()) == className) {
                keyTarget = widget;
                widget->setFocus();
                return;
            }
        }
    }
}

void place(const QStringList &words)
{
    QWidget *window = windowNamed(words.value(1));
    if (!window || words.size() < 6)
        return;
    window->setGeometry(words[2].toInt(), words[3].toInt(), words[4].toInt(), words[5].toInt());
}

void grabWindows(const QString &name)
{
    const QDir folder(shotsFolder());
    if (QWidget *window = mainWindow())
        window->grab().save(folder.filePath(name + QStringLiteral("-main.png")));
    if (QWidget *window = terminalsWindow())
        window->grab().save(folder.filePath(name + QStringLiteral("-terminals.png")));
}

void grabScreen(const QString &name)
{
    QScreen *screen = QGuiApplication::primaryScreen();
    screen->grabWindow(0).save(QDir(shotsFolder()).filePath(name + QStringLiteral(".png")));
}

void dumpWidget(QTextStream &out, const QWidget *widget, int depth)
{
    if (!widget->isVisible())
        return;
    out << QString(qsizetype(depth) * 2, QLatin1Char(' ')) << widget->metaObject()->className() << ' '
        << widget->objectName() << ' ' << widget->geometry().x() << ',' << widget->geometry().y() << ' '
        << widget->width() << 'x' << widget->height() << '\n';
    for (const QObject *child : widget->children()) {
        if (const auto *childWidget = qobject_cast<const QWidget *>(child))
            dumpWidget(out, childWidget, depth + 1);
    }
}

void dumpWidgets()
{
    QFile file(QDir(shotsFolder()).filePath(QStringLiteral("widgets.txt")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream out(&file);
    for (const QWidget *widget : QApplication::topLevelWidgets())
        dumpWidget(out, widget, 0);
}

void setTerminalsDetached(QMainWindow *window, bool detached)
{
    auto *page = window->findChild<DetachablePage *>();
    if (!page)
        return;
    if (detached)
        page->detach();
    else
        page->attach();
}

// The commands that act on the main window.
void runWindowCommand(QMainWindow *window, const QString &verb, const QString &argument)
{
    if (verb == QStringLiteral("mode"))
        showMode(window, argument);
    else if (verb == QStringLiteral("action"))
        triggerAction(window, argument);
    else if (verb == QStringLiteral("click"))
        clickButton(window, argument);
    else if (verb == QStringLiteral("detach") || verb == QStringLiteral("attach"))
        setTerminalsDetached(window, verb == QStringLiteral("detach"));
}

void runCommand(const QString &line)
{
    const QString verb = line.section(QLatin1Char(' '), 0, 0);
    const QString argument = line.section(QLatin1Char(' '), 1);
    if (verb == QStringLiteral("quit"))
        _exit(0); // without saving the window's settings
    if (verb == QStringLiteral("focus"))
        focusWidgetOfClass(argument);
    else if (verb == QStringLiteral("type"))
        typeText(argument);
    else if (verb == QStringLiteral("key"))
        pressKeys(argument);
    else if (verb == QStringLiteral("place"))
        place(line.split(QLatin1Char(' ')));
    else if (verb == QStringLiteral("press"))
        pressAt(line.split(QLatin1Char(' ')));
    else if (verb == QStringLiteral("grab"))
        grabWindows(argument);
    else if (verb == QStringLiteral("screen"))
        grabScreen(argument);
    else if (verb == QStringLiteral("dump"))
        dumpWidgets();
    else if (QMainWindow *window = mainWindow())
        runWindowCommand(window, verb, argument);
}

void pollCommands()
{
    const QDir folder(shotsFolder());
    QFile file(folder.filePath(QStringLiteral("command")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    file.remove();
    for (const QString &line : lines)
        runCommand(line);
    QFile done(folder.filePath(QStringLiteral("done")));
    if (done.open(QIODevice::WriteOnly))
        done.close();
}

void startPolling()
{
    if (shotsFolder().isEmpty())
        return;
    auto *timer = new QTimer(qApp);
    QObject::connect(timer, &QTimer::timeout, qApp, pollCommands);
    timer->start(pollMs);
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(startPolling)
