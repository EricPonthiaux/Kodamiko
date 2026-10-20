// SPDX-License-Identifier: GPL-3.0-only

#include "JavaRuntimes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace JavaRuntimes {
namespace {

#ifdef Q_OS_WIN
const QString javaName = QStringLiteral("java.exe");
#else
const QString javaName = QStringLiteral("java");
#endif

// The version in the JDK's "release" file, without running Java.
QString releaseVersion(const QString &home)
{
    QFile file(home + QStringLiteral("/release"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    static const QRegularExpression field(QStringLiteral("^JAVA_VERSION=\"([^\"]+)\""),
                                          QRegularExpression::MultilineOption);
    return field.match(QString::fromUtf8(file.readAll())).captured(1);
}

// "java -version" as a last resort (an old JDK without "release").
QString runVersion(const QString &executable)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executable, {QStringLiteral("-version")});
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished();
        return {};
    }
    static const QRegularExpression version(QStringLiteral("version \"([^\"]+)\""));
    return version.match(QString::fromUtf8(process.readAll())).captured(1);
}

// Folders whose subfolders are JDKs.
QStringList jdkParents()
{
    const QString home = QDir::homePath();
    QStringList parents{home + QStringLiteral("/.sdkman/candidates/java"), home + QStringLiteral("/.jdks"),
                        home + QStringLiteral("/.asdf/installs/java"),
                        home + QStringLiteral("/.local/share/mise/installs/java")};
#ifdef Q_OS_WIN
    for (const char *variable : {"ProgramFiles", "ProgramW6432"}) {
        const QString programFiles = qEnvironmentVariable(variable);
        if (programFiles.isEmpty())
            continue;
        for (const char *vendor :
             {"Java", "Eclipse Adoptium", "Microsoft", "Zulu", "Amazon Corretto", "BellSoft", "Semeru", "OpenJDK"})
            parents.append(programFiles + QLatin1Char('/') + QLatin1String(vendor));
    }
#elif defined(Q_OS_MACOS)
    parents << QStringLiteral("/Library/Java/JavaVirtualMachines")
            << home + QStringLiteral("/Library/Java/JavaVirtualMachines") << QStringLiteral("/opt/homebrew/opt")
            << QStringLiteral("/usr/local/opt");
#else
    parents << QStringLiteral("/usr/lib/jvm") << QStringLiteral("/usr/lib64/jvm") << QStringLiteral("/usr/java")
            << QStringLiteral("/opt/java") << QStringLiteral("/opt");
#endif
    return parents;
}

// The JDK folders inside `folder` (macOS bundles keep it in Contents/Home).
QStringList homesIn(const QString &folder)
{
    QStringList homes;
    const QDir dir(folder);
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = dir.filePath(name);
        for (const QString &candidate : {path, path + QStringLiteral("/Contents/Home"),
                                         path + QStringLiteral("/libexec/openjdk.jdk/Contents/Home")}) {
            if (QFileInfo(candidate + QStringLiteral("/bin/") + javaName).isExecutable())
                homes.append(candidate);
        }
    }
    return homes;
}

} // namespace

int parseMajorVersion(const QString &version)
{
    static const QRegularExpression number(QStringLiteral("^(\\d+)(?:\\.(\\d+))?"));
    const QRegularExpressionMatch match = number.match(version.trimmed());
    if (!match.hasMatch())
        return 0;
    const int first = match.captured(1).toInt();
    // "1.8.0": the old numbering, where the major version comes second.
    return first == 1 ? match.captured(2).toInt() : first;
}

std::optional<JavaRuntime> inspect(const QString &home)
{
    const QString executable = home + QStringLiteral("/bin/") + javaName;
    if (!QFileInfo(executable).isExecutable())
        return std::nullopt;
    QString version = releaseVersion(home);
    if (version.isEmpty())
        version = runVersion(executable);
    const int major = parseMajorVersion(version);
    if (major == 0)
        return std::nullopt;
    return JavaRuntime{QDir::cleanPath(home), QDir::cleanPath(executable), major};
}

QList<JavaRuntime> find()
{
    QStringList homes;
    const QString javaHome = qEnvironmentVariable("JAVA_HOME");
    if (!javaHome.isEmpty())
        homes.append(javaHome);
    // The java of the PATH: its JDK is two levels above the real file.
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("java"));
    if (!onPath.isEmpty())
        homes.append(QFileInfo(QFileInfo(onPath).canonicalFilePath()).dir().absoluteFilePath(QStringLiteral("..")));
    for (const QString &parent : jdkParents())
        homes.append(homesIn(parent));

    QList<JavaRuntime> runtimes;
    QSet<QString> seen; // the same JDK through several links
    for (const QString &home : homes) {
        const QString canonical = QFileInfo(home).canonicalFilePath();
        if (canonical.isEmpty() || seen.contains(canonical))
            continue;
        seen.insert(canonical);
        if (std::optional<JavaRuntime> runtime = inspect(canonical))
            runtimes.append(*runtime);
    }
    std::stable_sort(runtimes.begin(), runtimes.end(),
                     [](const JavaRuntime &a, const JavaRuntime &b) { return a.majorVersion > b.majorVersion; });
    return runtimes;
}

std::optional<JavaRuntime> newest(int minimumMajor)
{
    const QList<JavaRuntime> runtimes = find();
    if (runtimes.isEmpty() || runtimes.first().majorVersion < minimumMajor)
        return std::nullopt;
    return runtimes.first();
}

std::optional<JavaRuntime> newestOfAll()
{
    const QList<JavaRuntime> runtimes = find();
    return runtimes.isEmpty() ? std::nullopt : std::optional<JavaRuntime>(runtimes.first());
}

} // namespace JavaRuntimes
