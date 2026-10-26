// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <QStringList>

// Starting another Duet process, one per project window, the way the
// system expects an application to be started. One source per system.
namespace NewInstance {

struct Command {
    QString program;
    QStringList arguments;
};

// The command that starts a new Duet on `folder`, an absolute path, for
// QProcess::startDetached.
[[nodiscard]] Command command(const QString &folder);

} // namespace NewInstance
