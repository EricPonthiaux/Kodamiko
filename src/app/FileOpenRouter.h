// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>
#include <QString>

#include <functional>

// Opens the folders the system hands Duet as a QFileOpenEvent: on macOS, a
// folder dropped on the Dock icon, or opened with Duet from the Finder or
// `open -a`. They do not come through the command line. Elsewhere no such
// event arrives.
//
// A folder already open in a window brings that window forward. Otherwise
// it opens in this process if it has no project window yet (only the
// welcome window, at launch), else in a new process: one per project.
class FileOpenRouter : public QObject {
    Q_OBJECT

public:
    using OpenHere = std::function<void(const QString &folder)>;

    // Watches the application's events; `openHere` opens a folder in this
    // process, in place of the welcome window.
    FileOpenRouter(OpenHere openHere, QObject *parent);

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void open(const QString &folder);

    OpenHere openHere_;
};
