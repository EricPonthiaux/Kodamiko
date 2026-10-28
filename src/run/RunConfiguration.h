// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// A command the project is built, run, debugged or tested with, in the manner of
// IntelliJ's run configurations: run by the system's shell in a terminal
// tab, from a folder of the project.
struct RunConfiguration {
    enum class Kind {
        Build,
        Run,
        Debug, // runs the program under a debugger, or waiting for one
        Test
    };

    QString name;
    Kind kind = Kind::Run;
    QString command;                    // a shell command line ("cmake --build build")
    QString folder;                     // relative to the project; empty: its root
    QMap<QString, QString> environment; // added to the editor's
    // Those of Host::systemName() it is offered on; empty: every system.
    QStringList systems;
    // Found in the project's build files rather than written in
    // .kodamiko/run.json.
    bool detected = false;

    bool operator==(const RunConfiguration &) const = default;
};

namespace RunConfigurations {

// "build", "run", "debug", "test", and back (Run for an unknown name).
QString kindName(RunConfiguration::Kind kind);
RunConfiguration::Kind kindNamed(const QString &name);

// <project>/.kodamiko/run.json.
QString filePath(const QString &projectFolder);

// The configurations of a run.json:
//   {"configurations": [{"name": "Build", "kind": "build",
//                        "command": "cmake --build build", "folder": "",
//                        "env": {"VERBOSE": "1"}, "systems": ["linux", "macos"]}]}
// An entry without a name or a command is skipped; `error` says what is
// wrong with a file that cannot be read.
QList<RunConfiguration> parse(const QByteArray &json, QString *error = nullptr);
QByteArray serialize(const QList<RunConfiguration> &configurations);

// Whether `configuration` is offered on the system Kodamiko runs on.
bool isForThisSystem(const RunConfiguration &configuration);

// The configurations the build files at the project's root offer:
// package.json scripts, Maven, Cargo, Makefile. Builds first. CMake
// projects offer none: their builds are those of .kodamiko/run.json.
QList<RunConfiguration> detect(const QString &projectFolder);

// Those of .kodamiko/run.json for this system, then the detected ones whose
// name they do not reuse (a configuration of the file replaces the detected
// one).
QList<RunConfiguration> all(const QString &projectFolder, QString *error = nullptr);

} // namespace RunConfigurations
