#pragma once

#include "projects/ProjectConfig.h"

#include <QList>
#include <QString>

class Toolchains;

// One tool to download for a project.
struct InstallStep {
    QString tool;    // a ToolCatalog id
    QString version; // "17", "23.1.0"
    QString title;   // "JDK 17", "clangd 23.1.0"
    QString reason;  // "Java runtime", "runs jdtls"
    bool operator==(const InstallStep &) const = default;
};

namespace InstallPlan {

// What `config` needs that is neither downloaded nor on the machine,
// runtimes first: each enabled language's runtime, its server, and what the
// server runs on (jdtls needs Java 21 even for a Java 17 project).
QList<InstallStep> plan(const ProjectConfig &config, const Toolchains &toolchains);
// What is missing that Duet does not download: a Python, a server set to
// the PATH's that is not there. Sentences to show.
QStringList unavailable(const ProjectConfig &config, const Toolchains &toolchains);
// "JDK 17, jdtls 1.62.0".
QString summary(const QList<InstallStep> &steps);

} // namespace InstallPlan
