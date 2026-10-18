#pragma once

#include "terminal/ProcessSpec.h"

#include <QList>
#include <QString>
#include <QUrl>
#include <QVersionNumber>

// External programs the editor relies on: git (Diff mode operations,
// snapshots) and Claude Code (the Claude session). On a new machine,
// they may be missing or too old.
struct Prerequisite {
    enum class Id {
        Git,
        ClaudeCode
    };
    enum class Status {
        Missing,
        Outdated,
        Ok
    };

    Id id = Id::Git;
    QString name;
    QString purpose; // why the editor needs it
    Status status = Status::Missing;
    QString path;
    QVersionNumber version;
    QVersionNumber minimumVersion;
    // Command that installs (or updates) it, run in an editor terminal;
    // empty when there is no reliable command for this system.
    QString installCommand;
    QUrl helpUrl; // installation instructions
};

namespace Prerequisites {

// Without running anything: whether git and claude can be found.
bool allFound();

// Finds the programs and asks their versions (a few hundred milliseconds).
QList<Prerequisite> check();

// First "x.y.z" number of a "--version" output.
QVersionNumber parseVersion(const QString &output);

// Package manager command installing `package`, deduced from the ID and
// ID_LIKE fields of /etc/os-release; empty for an unknown distribution.
QString linuxInstallCommand(const QString &osId, const QString &osIdLike, const QString &package);

// Process running `prerequisite.installCommand` in a terminal.
ProcessSpec installProcess(const Prerequisite &prerequisite, const QString &workingDirectory);

} // namespace Prerequisites
