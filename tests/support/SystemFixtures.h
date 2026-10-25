// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

// What the tests need of each system's programs and shell. One
// implementation per system: SystemFixturesWindows.cpp,
// SystemFixturesUnix.cpp.
namespace SystemFixtures {

// A program every machine has in its PATH: cmd on Windows, sh elsewhere.
QString programOfEveryMachine();

// A command line of the system's shell (Shell::systemShellRunning) that
// prints `text`, then exits with `exitCode`.
QString printingThenExiting(const QString &text, int exitCode);

// A command line of the system's shell that succeeds when the file
// `fileName` of its working folder exists, and fails otherwise.
QString succeedingIfFileExists(const QString &fileName);

} // namespace SystemFixtures
