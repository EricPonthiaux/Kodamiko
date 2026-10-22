// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QLocalServer>
#include <QObject>

// Makes the window that has a project open findable by the other Duet
// processes, which each hold one window: opening a project already open
// elsewhere brings that window to the front instead of opening it twice.
//
// Protocol: a local socket (named pipe on Windows) named after the
// project's folder, for the user only; a connection to it is a request to
// show the window, nothing is exchanged.
class ProjectWindowServer : public QObject {
    Q_OBJECT

public:
    explicit ProjectWindowServer(QObject *parent = nullptr);

    // Answers for `folder` from now on, and no longer for the previous one.
    // Takes the place of another process answering for it: check raise()
    // first. False if the socket could not be created: the window then
    // works, but cannot be found.
    bool listen(const QString &folder);

signals:
    // Another process opened the project: the window should come forward.
    void raiseRequested();

private:
    void onNewConnection();

    QLocalServer server_;
};

namespace ProjectWindows {

// Asks the process that has `folder` open to bring its window forward;
// false if no process has it open.
[[nodiscard]] bool raise(const QString &folder);

} // namespace ProjectWindows
