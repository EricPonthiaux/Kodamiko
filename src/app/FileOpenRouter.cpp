// SPDX-License-Identifier: GPL-3.0-only

#include "FileOpenRouter.h"

#include "MainWindow.h"
#include "projects/NewInstance.h"
#include "projects/ProjectWindowServer.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QProcess>

#include <algorithm>
#include <utility>

namespace {

bool hasProjectWindow()
{
    const QWidgetList windows = QApplication::topLevelWidgets();
    return std::ranges::any_of(windows, [](QWidget *window) { return qobject_cast<MainWindow *>(window); });
}

} // namespace

FileOpenRouter::FileOpenRouter(OpenHere openHere, QObject *parent)
    : QObject(parent)
    , openHere_(std::move(openHere))
{
    qApp->installEventFilter(this);
}

bool FileOpenRouter::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::FileOpen)
        return QObject::eventFilter(watched, event);
    const QString path = static_cast<QFileOpenEvent *>(event)->file();
    if (!QFileInfo(path).isDir()) {
        qWarning("Duet opens folders, not files: %s was not opened", qPrintable(QDir::toNativeSeparators(path)));
        return true;
    }
    open(QDir(path).absolutePath());
    return true;
}

void FileOpenRouter::open(const QString &folder)
{
    if (ProjectWindows::raise(folder))
        return;
    if (!hasProjectWindow()) {
        openHere_(folder);
        return;
    }
    const NewInstance::Command command = NewInstance::command(folder);
    if (!QProcess::startDetached(command.program, command.arguments))
        qWarning("Could not open %s in a new window: %s did not run", qPrintable(QDir::toNativeSeparators(folder)),
                 qPrintable(command.program));
}
