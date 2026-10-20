// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QString>

#include <optional>

// Java installations of the machine. An application started from the
// desktop does not get the shell's environment (SDKMAN, JAVA_HOME set in
// .bashrc): the JDKs are looked for where installers put them.
struct JavaRuntime {
    QString home;       // the JDK folder (with bin/java)
    QString executable; // bin/java
    int majorVersion = 0;
};

namespace JavaRuntimes {

// Every Java found: JAVA_HOME, the java of the PATH, SDKMAN, IntelliJ's
// downloads (~/.jdks), /usr/lib/jvm, /Library/Java/JavaVirtualMachines,
// Program Files... Newest first.
QList<JavaRuntime> find();
// The newest one of at least `minimumMajor`, if any.
std::optional<JavaRuntime> newest(int minimumMajor);
// The newest one of all, to say what was found when it is too old.
std::optional<JavaRuntime> newestOfAll();

// 8 for "1.8.0_504", 25 for "25.0.3", 21 for "21"; 0 if unreadable.
int parseMajorVersion(const QString &version);
// The Java of the JDK folder `home`, if it has one.
std::optional<JavaRuntime> inspect(const QString &home);

} // namespace JavaRuntimes
