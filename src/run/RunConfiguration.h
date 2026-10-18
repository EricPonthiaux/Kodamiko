#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>

// A command the project is built, run or tested with, in the manner of
// IntelliJ's run configurations: run by the system's shell in a terminal
// tab, from a folder of the project.
struct RunConfiguration {
    enum class Kind { Build, Run, Test };

    QString name;
    Kind kind = Kind::Run;
    QString command; // a shell command line ("cmake --build build")
    QString folder;  // relative to the project; empty: its root
    QMap<QString, QString> environment; // added to the editor's
    // Found in the project's build files rather than written in
    // .duet/run.json.
    bool detected = false;

    bool operator==(const RunConfiguration &) const = default;
};

namespace RunConfigurations {

// "build", "run", "test", and back (Run for an unknown name).
QString kindName(RunConfiguration::Kind kind);
RunConfiguration::Kind kindNamed(const QString &name);

// <project>/.duet/run.json.
QString filePath(const QString &projectFolder);

// The configurations of a run.json:
//   {"configurations": [{"name": "Build", "kind": "build",
//                        "command": "cmake --build build", "folder": "",
//                        "env": {"VERBOSE": "1"}}]}
// An entry without a name or a command is skipped; `error` says what is
// wrong with a file that cannot be read.
QList<RunConfiguration> parse(const QByteArray &json, QString *error = nullptr);
QByteArray serialize(const QList<RunConfiguration> &configurations);

// The configurations the build files at the project's root offer:
// CMakePresets.json (or CMakeLists.txt), package.json scripts, Maven,
// Gradle, Cargo, Makefile. Builds first.
QList<RunConfiguration> detect(const QString &projectFolder);

// Those of .duet/run.json, then the detected ones whose name the file does
// not reuse (a configuration of the file replaces the detected one).
QList<RunConfiguration> all(const QString &projectFolder, QString *error = nullptr);

} // namespace RunConfigurations
