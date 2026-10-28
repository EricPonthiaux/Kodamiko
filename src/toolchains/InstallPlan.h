// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ToolCatalog.h"
#include "projects/ProjectConfig.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

class Toolchains;

// One tool to download for a project.
struct InstallStep {
    QString tool;    // a ToolCatalog id
    QString version; // "17", "23.1.0"
    QString title;   // "JDK 17", "clangd 23.1.0"
    QString reason;  // "Java runtime", "runs jdtls"
    // For Qt: the kit ("msvc2022_64") and its add-on modules ("qtcharts").
    QString kit;
    QStringList modules;
    // For a tool its recipe describes: where to download it, and the file,
    // relative to its folder, without which the install is not usable.
    std::optional<DownloadSource> source;
    QString check;
    bool operator==(const InstallStep &) const = default;
};

// A runtime a project needs whatever its languages, because its recipe
// requires it (see Recipe::requirements).
struct ToolRequirement {
    // A ToolCatalog runtime ("jdk", "node"), "qt", a tool the requirement
    // says how to download ("maven"), or a program Kodamiko only looks for in
    // the PATH ("make").
    QString tool;
    // A major version ("21"); for Qt, a version prefix ("6.10": the newest
    // 6.10.x); for a tool with a download, the exact version. Empty: any,
    // the default or newest one if none is there. A program of the PATH is
    // taken whatever its version.
    QString version;
    QString kit;         // Qt's: "msvc2022_64", "gcc_64", "macos"
    QStringList modules; // Qt's add-ons: "qtcharts"
    // Its command ("mvn"; empty: `tool`): in the PATH, it stands for the
    // tool, and nothing is downloaded.
    QString program;
    // Where to download it on this system (Kodamiko installs it in
    // <store>/<tool>/<version>), and the file proving the install
    // (InstallStep::check). Nullopt: a ToolCatalog tool, Qt, or a program
    // of the PATH.
    std::optional<DownloadSource> download;
    QString check;
    bool operator==(const ToolRequirement &) const = default;
};

namespace InstallPlan {

// What `config` and `required` need that is neither downloaded nor on the
// machine, runtimes first: the required runtimes, each enabled language's
// runtime, its server, and what the server runs on (jdtls needs Java 21
// even for a Java 17 project).
QList<InstallStep> plan(const ProjectConfig &config,
                        const Toolchains &toolchains,
                        const QList<ToolRequirement> &required);
// What is missing that Kodamiko does not download: a Python, a server set to
// the PATH's that is not there, a required tool or version it does not
// offer. Sentences to show.
QStringList unavailable(const ProjectConfig &config,
                        const Toolchains &toolchains,
                        const QList<ToolRequirement> &required);
// "JDK 17, jdtls 1.62.0".
QString summary(const QList<InstallStep> &steps);

} // namespace InstallPlan
