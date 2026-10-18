#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

// A runtime or language server Duet can download: where its builds are
// published, and what it runs on.
struct ToolSpec {
    QString id;    // "jdk", "node", "clangd", "jdtls", "typescript-language-server", "pyright"
    QString title; // "JDK", "Node.js", "clangd"...
    bool isRuntime = false;
    QString family; // the ProjectLanguages family it serves
    // Offered, newest first (for runtimes: the major versions).
    QStringList versions;
    // Its command in the PATH ("pyright-langserver"), for "system".
    QString program;
    // In Duet's install of it, the first one present: the executable, or
    // the script run with Node (see `script`).
    QStringList executables;
    bool script = false;
    // What it needs to run: a JDK of 21 or more for jdtls, Node for the
    // npm packages. Empty: nothing.
    QString runtime;
    int minimumRuntime = 0;
    // Installed with npm, in this order; "{version}" is the chosen one.
    QStringList npmPackages;
};

// A system and processor, as the builds are named.
struct Platform {
    QString os;   // "linux", "mac", "windows"
    QString arch; // "x64", "aarch64"
};

enum class ArchiveFormat {
    TarGz,
    Zip,
    Npm
};

// Where to download one version of a tool.
struct DownloadSource {
    QUrl url;
    QString fileName;
    // A file giving its SHA-256; empty when none is published.
    QUrl checksumUrl;
    ArchiveFormat format = ArchiveFormat::TarGz;
    // Why it cannot be downloaded (no build for this platform...).
    QString error;
};

namespace ToolCatalog {

QList<ToolSpec> all();
const ToolSpec *find(const QString &id);
// The servers of `family`, preferred first.
QStringList servers(const QString &family);
// The tool of the runtime of `family`: "jdk", "node"; empty for the
// languages whose runtime Duet does not download (C++, Python).
QString runtimeOf(const QString &family);
// The runtime versions offered for `family` (C++: the standards), newest
// first, and the one chosen when the project does not say.
QStringList runtimeVersions(const QString &family);
QString defaultRuntimeVersion(const QString &family);

Platform currentPlatform();

// What to fetch first to know where the archive is (a release's assets,
// the list of versions); empty when the address is known already.
QUrl metadataUrl(const QString &id, const QString &version);
// The archive of `id` `version` for `platform`, given what metadataUrl
// returned.
DownloadSource source(const QString &id, const QString &version, const Platform &platform, const QByteArray &metadata);
// The SHA-256 of `fileName` in a checksum file: a single hash, or lines of
// "hash  name". Empty if it is not there.
QString checksumFor(const QByteArray &checksums, const QString &fileName);

} // namespace ToolCatalog
