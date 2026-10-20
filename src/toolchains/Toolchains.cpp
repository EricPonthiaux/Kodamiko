// SPDX-License-Identifier: GPL-3.0-only

#include "Toolchains.h"

#include "JavaRuntimes.h"
#include "ToolCatalog.h"
#include "projects/ProjectDetector.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QVersionNumber>

#include <algorithm>
#include <utility>

namespace {

// What was found on the machine, by tool, for the whole application.
QHash<QString, QList<RuntimeInstall>> &probedRuntimes()
{
    static QHash<QString, QList<RuntimeInstall>> runtimes;
    return runtimes;
}

const QString completeMarker = QStringLiteral(".complete");

// The output of `executable --version`, empty if it does not answer.
QString runVersion(const QString &executable)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executable, {QStringLiteral("--version")});
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished();
        return {};
    }
    return QString::fromUtf8(process.readAll()).trimmed();
}

int major(const QString &version)
{
    return version.section(QLatin1Char('.'), 0, 0).toInt();
}

bool newerFirst(const QString &a, const QString &b)
{
    return QVersionNumber::fromString(a) > QVersionNumber::fromString(b);
}

// The runtime at `path` (its home or its executable), among those found
// or as it is.
std::optional<RuntimeInstall> runtimeAt(const QString &path,
                                        const QString &tool,
                                        const QString &version,
                                        const QList<RuntimeInstall> &found)
{
    for (const RuntimeInstall &runtime : found) {
        if (runtime.home == path || runtime.executable == path)
            return runtime;
    }
    // Not among those found (a project's .venv): taken as it is.
    if (QFileInfo(path).isExecutable() && tool == QLatin1String("python"))
        return RuntimeInstall{tool, version, QFileInfo(path).absolutePath(), path, false};
    if (tool != QLatin1String("jdk"))
        return std::nullopt;
    const std::optional<JavaRuntime> java = JavaRuntimes::inspect(path);
    if (!java)
        return std::nullopt;
    return RuntimeInstall{tool, QString::number(java->majorVersion), java->home, java->executable, false};
}

std::optional<RuntimeInstall> newest(const QList<RuntimeInstall> &runtimes)
{
    if (runtimes.isEmpty())
        return std::nullopt;
    return *std::max_element(runtimes.begin(), runtimes.end(), [](const RuntimeInstall &a, const RuntimeInstall &b) {
        return newerFirst(b.version, a.version);
    });
}

#ifdef Q_OS_WIN
const QStringList pythonNames{QStringLiteral("python"), QStringLiteral("python3")};
#else
const QStringList pythonNames{QStringLiteral("python3"),    QStringLiteral("python"),     QStringLiteral("python3.15"),
                              QStringLiteral("python3.14"), QStringLiteral("python3.13"), QStringLiteral("python3.12"),
                              QStringLiteral("python3.11"), QStringLiteral("python3.10"), QStringLiteral("python3.9"),
                              QStringLiteral("python3.8")};
#endif

// Version managers keep one folder per version.
QStringList versionManagerNodes()
{
    const QString home = QDir::homePath();
    QStringList executables;
    for (const QString &parent :
         {home + QStringLiteral("/.nvm/versions/node"), home + QStringLiteral("/.local/share/fnm/node-versions"),
          home + QStringLiteral("/.volta/tools/image/node")}) {
        for (const QString &name : QDir(parent).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            for (const QString &candidate :
                 {parent + QLatin1Char('/') + name + QStringLiteral("/bin/node"),
                  parent + QLatin1Char('/') + name + QStringLiteral("/installation/bin/node")}) {
                if (QFileInfo(candidate).isExecutable())
                    executables.append(candidate);
            }
        }
    }
    return executables;
}

QList<RuntimeInstall> machineNodes()
{
    QStringList executables;
    if (const QString onPath = QStandardPaths::findExecutable(QStringLiteral("node")); !onPath.isEmpty())
        executables.append(onPath);
    executables.append(versionManagerNodes());
    QList<RuntimeInstall> nodes;
    QSet<QString> seen;
    for (const QString &executable : std::as_const(executables)) {
        const QString canonical = QFileInfo(executable).canonicalFilePath();
        if (canonical.isEmpty() || seen.contains(canonical))
            continue;
        seen.insert(canonical);
        const QString version = runVersion(executable); // "v24.2.0"
        if (!version.startsWith(QLatin1Char('v')))
            continue;
        RuntimeInstall node;
        node.tool = QStringLiteral("node");
        node.version = QString::number(major(version.mid(1)));
        node.executable = executable;
        // bin/node: the install is the folder above.
        node.home = QDir::cleanPath(QFileInfo(canonical).absolutePath() + QStringLiteral("/.."));
        nodes.append(node);
    }
    return nodes;
}

QList<RuntimeInstall> machinePythons()
{
    QStringList executables;
    for (const QString &name : pythonNames) {
        if (const QString path = QStandardPaths::findExecutable(name); !path.isEmpty())
            executables.append(path);
    }
    const QString pyenv = QDir::homePath() + QStringLiteral("/.pyenv/versions");
    for (const QString &name : QDir(pyenv).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString candidate = pyenv + QLatin1Char('/') + name + QStringLiteral("/bin/python");
        if (QFileInfo(candidate).isExecutable())
            executables.append(candidate);
    }
    QList<RuntimeInstall> pythons;
    QSet<QString> seen;
    for (const QString &executable : std::as_const(executables)) {
        const QString canonical = QFileInfo(executable).canonicalFilePath();
        if (canonical.isEmpty() || seen.contains(canonical))
            continue;
        seen.insert(canonical);
        // "Python 3.12.3"; the Windows Store's stub prints nothing.
        const QString version = ProjectDetector::pythonMinor(runVersion(executable));
        if (version.isEmpty())
            continue;
        pythons.append({QStringLiteral("python"), version, QFileInfo(executable).absolutePath(), executable, false});
    }
    std::stable_sort(pythons.begin(), pythons.end(),
                     [](const RuntimeInstall &a, const RuntimeInstall &b) { return newerFirst(a.version, b.version); });
    return pythons;
}

} // namespace

Toolchains::Toolchains(QString root, bool lookOnMachine)
    : root_(std::move(root))
    , lookOnMachine_(lookOnMachine)
{
}

QString Toolchains::defaultRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/toolchains");
}

QString Toolchains::installFolder(const QString &tool, const QString &version) const
{
    return root_ + QLatin1Char('/') + tool + QLatin1Char('/') + version;
}

bool Toolchains::isInstalled(const QString &tool, const QString &version) const
{
    return !version.isEmpty() && QFileInfo::exists(installFolder(tool, version) + QLatin1Char('/') + completeMarker);
}

QStringList Toolchains::installedVersions(const QString &tool) const
{
    QStringList versions;
    for (const QString &version : QDir(root_ + QLatin1Char('/') + tool).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (isInstalled(tool, version))
            versions.append(version);
    }
    std::sort(versions.begin(), versions.end(), newerFirst);
    return versions;
}

QString Toolchains::installedExecutable(const QString &tool, const QString &version) const
{
    const ToolSpec *spec = ToolCatalog::find(tool);
    if (!spec || !isInstalled(tool, version))
        return {};
    const QDir folder(installFolder(tool, version));
    for (const QString &executable : spec->executables) {
        if (folder.exists(executable))
            return folder.filePath(executable);
    }
    return {};
}

QList<RuntimeInstall> Toolchains::managedRuntimes(const QString &tool) const
{
    QList<RuntimeInstall> runtimes;
    for (const QString &version : installedVersions(tool)) {
        const QString executable = installedExecutable(tool, version);
        if (executable.isEmpty())
            continue;
        // bin/java, bin/node: the install is above; node.exe sits at the top.
        QString home = QFileInfo(executable).absolutePath();
        if (QFileInfo(home).fileName() == QLatin1String("bin"))
            home = QDir::cleanPath(home + QStringLiteral("/.."));
        runtimes.append({tool, version, home, executable, true});
    }
    return runtimes;
}

QList<RuntimeInstall> Toolchains::machineRuntimes(const QString &tool) const
{
    if (!lookOnMachine_)
        return {};
    const auto it = machine_.find(tool);
    if (it != machine_.end())
        return it.value();
    // Probing runs programs ("node --version"...): its result is shared by
    // every Toolchains of the application (welcome window, main window).
    QHash<QString, QList<RuntimeInstall>> &probed = probedRuntimes();
    auto known = probed.find(tool);
    if (known == probed.end()) {
        QList<RuntimeInstall> found;
        if (tool == QLatin1String("jdk")) {
            for (const JavaRuntime &java : JavaRuntimes::find())
                found.append({tool, QString::number(java.majorVersion), java.home, java.executable, false});
        } else if (tool == QLatin1String("node")) {
            found = machineNodes();
        } else if (tool == QLatin1String("python")) {
            found = machinePythons();
        }
        known = probed.insert(tool, found);
    }
    QList<RuntimeInstall> runtimes = known.value();
    // Duet's own downloads are listed apart.
    const QString own = QFileInfo(root_).canonicalFilePath();
    if (!own.isEmpty()) {
        runtimes.removeIf([&own](const RuntimeInstall &runtime) {
            return QFileInfo(runtime.home).canonicalFilePath().startsWith(own + QLatin1Char('/'));
        });
    }
    machine_.insert(tool, runtimes);
    return runtimes;
}

QList<RuntimeInstall> Toolchains::runtimes(const QString &tool) const
{
    QList<RuntimeInstall> all = managedRuntimes(tool);
    QList<RuntimeInstall> machine = machineRuntimes(tool);
    std::stable_sort(machine.begin(), machine.end(),
                     [](const RuntimeInstall &a, const RuntimeInstall &b) { return newerFirst(a.version, b.version); });
    all.append(machine);
    return all;
}

std::optional<RuntimeInstall> Toolchains::runtime(const QString &tool,
                                                  const QString &version,
                                                  const QString &path) const
{
    const QList<RuntimeInstall> all = runtimes(tool);
    if (!path.isEmpty()) {
        if (std::optional<RuntimeInstall> atPath = runtimeAt(path, tool, version, all))
            return atPath;
    }
    if (version.isEmpty())
        return newest(all);
    for (const RuntimeInstall &runtime : all) {
        if (runtime.version == version)
            return runtime;
    }
    return std::nullopt;
}

std::optional<RuntimeInstall> Toolchains::runtimeAtLeast(const QString &tool, int minimumMajor) const
{
    std::optional<RuntimeInstall> best;
    for (const RuntimeInstall &runtime : runtimes(tool)) {
        if (major(runtime.version) >= minimumMajor && (!best || major(runtime.version) > major(best->version)))
            best = runtime;
    }
    return best;
}

std::optional<RuntimeInstall> Toolchains::runtimeFor(const LanguageSetup &setup) const
{
    QString tool = ToolCatalog::runtimeOf(setup.language);
    if (setup.language == QLatin1String("python"))
        tool = QStringLiteral("python");
    if (tool.isEmpty())
        return std::nullopt;
    return runtime(tool, setup.runtimeVersion, setup.runtimePath);
}

QString Toolchains::npmScript(const RuntimeInstall &node)
{
    for (const QString &relative :
         {QStringLiteral("/lib/node_modules/npm/bin/npm-cli.js"), QStringLiteral("/node_modules/npm/bin/npm-cli.js")}) {
        if (QFileInfo::exists(node.home + relative))
            return node.home + relative;
    }
    return {};
}

QString Toolchains::systemProgram(const QString &server) const
{
    if (!lookOnMachine_)
        return {};
    auto it = programs_.find(server);
    if (it == programs_.end()) {
        const ToolSpec *spec = ToolCatalog::find(server);
        it = programs_.insert(server, QStandardPaths::findExecutable(spec ? spec->program : server));
    }
    return it.value();
}

QString Toolchains::serverExecutable(const QString &server, const QString &version) const
{
    if (version.isEmpty() || version == QLatin1String(ProjectLanguages::systemVersion))
        return systemProgram(server);
    return installedExecutable(server, version);
}

void Toolchains::complete(LanguageSetup &setup) const
{
    if (setup.runtimeVersion.isEmpty())
        completeRuntimeVersion(setup);
    if (setup.server.isEmpty())
        completeServer(setup);
    if (setup.serverVersion.isEmpty() && !setup.server.isEmpty())
        completeServerVersion(setup);
}

void Toolchains::completeRuntimeVersion(LanguageSetup &setup) const
{
    if (setup.language != QLatin1String("python")) {
        setup.runtimeVersion = ToolCatalog::defaultRuntimeVersion(setup.language);
        return;
    }
    if (const std::optional<RuntimeInstall> python = runtime(QStringLiteral("python"), {}, setup.runtimePath))
        setup.runtimeVersion = python->version;
}

void Toolchains::completeServer(LanguageSetup &setup) const
{
    const QStringList servers = ToolCatalog::servers(setup.language);
    // The one of the PATH if there is one, else the preferred one.
    const auto onPath =
        std::ranges::find_if(servers, [this](const QString &server) { return !systemProgram(server).isEmpty(); });
    if (onPath != servers.end())
        setup.server = *onPath;
    else if (!servers.isEmpty())
        setup.server = servers.first();
}

void Toolchains::completeServerVersion(LanguageSetup &setup) const
{
    const ToolSpec *spec = ToolCatalog::find(setup.server);
    const QStringList installed = installedVersions(setup.server);
    if (!systemProgram(setup.server).isEmpty() || !spec)
        setup.serverVersion = QLatin1String(ProjectLanguages::systemVersion);
    else if (!installed.isEmpty())
        setup.serverVersion = installed.first();
    else
        setup.serverVersion = spec->versions.value(0);
}

void Toolchains::refresh()
{
    machine_.clear();
    programs_.clear();
    probedRuntimes().clear();
}
