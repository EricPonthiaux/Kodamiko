// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

// The log files of Kodamiko's programs (the editor, kodamiko-hook, kodamiko-mcp,
// kodamiko-askpass): every Qt message of a process, whatever its level
// (qDebug… qFatal) and category, goes to a file of its own in folder(), and
// still to the standard error.
//
// Nothing is ever deleted: during the acceptance testing, testers zip the
// folder and send it back whole.
namespace LogFiles {

// ~/.kodamiko/logs, or $KDK_LOG_DIR when it is set.
QString folder();

// Routes the process's messages to
// <folder>/<program>-<yyyyMMdd-HHmmss>-<pid>.log, created at the first
// message: a run that logs nothing leaves no file. The file starts with the
// version, the build type and the system, and is flushed after each message
// so that a crash loses nothing.
//
// Call once, after the QCoreApplication exists; messages go back to the
// standard error alone when it is destroyed.
void install(const QString &program);

} // namespace LogFiles
