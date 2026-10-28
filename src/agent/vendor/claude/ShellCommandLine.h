// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// A shell command line read as its simple commands, the way a POSIX shell
// splits them: enough to find the programs it runs and their arguments,
// not a shell (nothing is expanded).
namespace ShellCommandLine {

// The simple commands of `line`, in order, each as its words once quotes
// and backslashes are removed: `cd src && git commit -m "a b"` is
// {cd, src} then {git, commit, -m, a b}. ; | & && || and a newline end a
// command; & inside a redirection (2>&1) does not.
QList<QStringList> commands(const QString &line);

} // namespace ShellCommandLine
