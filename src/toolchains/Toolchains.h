// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projects/ProjectConfig.h"

#include <QHash>
#include <QList>
#include <QString>

#include <optional>

// A runtime a project can use: one Kodamiko downloaded, or one of the machine.
struct RuntimeInstall {
    QString tool;    // "jdk", "node", "python"
    QString version; // "17", "24" (major versions), "3.12"
    QString home;    // the JDK or Node folder; for Python, its executable's
    QString executable;
    bool managed = false; // downloaded by Kodamiko
};

// The runtimes and language servers available: those Kodamiko downloaded, in
// its store (<root>/<tool>/<version>, complete once a ".complete" file is
// there), and those of the machine (the PATH, JavaRuntimes, nvm,
// pyenv...). What is found on the machine is kept until refresh().
class Toolchains {
public:
    // `lookOnMachine` false: only Kodamiko's own installs (tests).
    explicit Toolchains(QString root = defaultRoot(), bool lookOnMachine = true);
    // ~/.kodamiko/tools: the store shared by every project and every version of
    // Kodamiko, so that nothing is downloaded twice.
    static QString defaultRoot();

    QString root() const { return root_; }

    // Kodamiko's own installs.
    QString installFolder(const QString &tool, const QString &version) const;
    bool isInstalled(const QString &tool, const QString &version) const;
    // Newest first.
    QStringList installedVersions(const QString &tool) const;
    // The executable of Kodamiko's install (ToolSpec::executables), empty if
    // it is not installed.
    QString installedExecutable(const QString &tool, const QString &version) const;

    // The runtimes of `tool`, Kodamiko's first, then newest first.
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

    // The command of `tool` (a ToolCatalog id, or a program's name: "mvn")
    // in the PATH, empty if there is none.
    QString systemProgram(const QString &tool) const;
    // The server's executable for `version`: Kodamiko's install, or the PATH's
    // for "system". Empty when it is not there.
    QString serverExecutable(const QString &server, const QString &version) const;

    // Kodamiko's install of Qt `version` ("6.10.3") for `kit`:
    // <root>/qt/<version>/<kit>, as Qt's installer lays it out.
    QString qtInstallFolder(const QString &version, const QString &kit) const;
    // The newest Qt for `kit` whose version starts with `version` ("6.10";
    // empty: any): Kodamiko's, or one in the machine's usual folders (see
    // machineQtRoots). Kodamiko's must have `modules`; one installed otherwise
    // is taken as it is. Empty when there is none.
    QString qtFolder(const QString &version, const QString &kit, const QStringList &modules = {}) const;
    // C:/Qt and ~/Qt, and /opt/Qt outside Windows.
    static QStringList machineQtRoots();

    // Fills what `setup` leaves empty: the runtime version, the preferred
    // server, and its version (the PATH's when it has one, else the
    // newest).
    void complete(LanguageSetup &setup) const;

    // Looks on the machine again, next time.
    void refresh();

private:
    QList<RuntimeInstall> managedRuntimes(const QString &tool) const;
    QList<RuntimeInstall> machineRuntimes(const QString &tool) const;
    // The parts of complete(), each called when its field is empty.
    void completeRuntimeVersion(LanguageSetup &setup) const;
    void completeServer(LanguageSetup &setup) const;
    void completeServerVersion(LanguageSetup &setup) const;

    QString root_;
    bool lookOnMachine_;
    mutable QHash<QString, QList<RuntimeInstall>> machine_; // by tool
    mutable QHash<QString, QString> programs_;              // by server
};

// Where Kodamiko downloaded its tools before ~/.kodamiko/tools.
namespace LegacyToolchains {

// <application data>/toolchains.
QString defaultRoot();

// Moves each <tool>/<version> of `from` into `to`, unless `to` has it
// already, then removes `from` if nothing is left in it. Returns the
// folders that could not be moved, empty when all were.
[[nodiscard]] QStringList moveInto(const QString &from, const QString &to);

} // namespace LegacyToolchains
