#pragma once

#include "projects/ProjectConfig.h"

#include <QHash>
#include <QList>
#include <QString>

#include <optional>

// A runtime a project can use: one Duet downloaded, or one of the machine.
struct RuntimeInstall {
    QString tool;    // "jdk", "node", "python"
    QString version; // "17", "24" (major versions), "3.12"
    QString home;    // the JDK or Node folder; for Python, its executable's
    QString executable;
    bool managed = false; // downloaded by Duet
};

// The runtimes and language servers available: those Duet downloaded, in
// its own folder (<root>/<tool>/<version>, complete once a ".complete"
// file is there), and those of the machine (the PATH, JavaRuntimes, nvm,
// pyenv...). What is found on the machine is kept until refresh().
class Toolchains {
public:
    // `lookOnMachine` false: only Duet's own installs (tests).
    explicit Toolchains(QString root = defaultRoot(), bool lookOnMachine = true);
    // <application data>/toolchains.
    static QString defaultRoot();

    QString root() const { return root_; }

    // Duet's own installs.
    QString installFolder(const QString &tool, const QString &version) const;
    bool isInstalled(const QString &tool, const QString &version) const;
    // Newest first.
    QStringList installedVersions(const QString &tool) const;
    // The executable of Duet's install (ToolSpec::executables), empty if
    // it is not installed.
    QString installedExecutable(const QString &tool, const QString &version) const;

    // The runtimes of `tool`, Duet's first, then newest first.
    QList<RuntimeInstall> runtimes(const QString &tool) const;
    // The runtime at `path` if given and present, else one of `version`
    // (empty: the newest).
    std::optional<RuntimeInstall> runtime(const QString &tool, const QString &version, const QString &path = {}) const;
    // The newest of major version `minimumMajor` or more.
    std::optional<RuntimeInstall> runtimeAtLeast(const QString &tool, int minimumMajor) const;
    // The runtime `setup` names (its JDK, Node or Python), if present.
    std::optional<RuntimeInstall> runtimeFor(const LanguageSetup &setup) const;
    // npm's script in a Node install, empty if it has none.
    static QString npmScript(const RuntimeInstall &node);

    // `server`'s command in the PATH, empty if there is none.
    QString systemProgram(const QString &server) const;
    // The server's executable for `version`: Duet's install, or the PATH's
    // for "system". Empty when it is not there.
    QString serverExecutable(const QString &server, const QString &version) const;

    // Fills what `setup` leaves empty: the runtime version, the preferred
    // server, and its version (the PATH's when it has one, else the
    // newest).
    void complete(LanguageSetup &setup) const;

    // Looks on the machine again, next time.
    void refresh();

private:
    QList<RuntimeInstall> managedRuntimes(const QString &tool) const;
    QList<RuntimeInstall> machineRuntimes(const QString &tool) const;

    QString root_;
    bool lookOnMachine_;
    mutable QHash<QString, QList<RuntimeInstall>> machine_; // by tool
    mutable QHash<QString, QString> programs_;              // by server
};
