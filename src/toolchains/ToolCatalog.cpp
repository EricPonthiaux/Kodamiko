// SPDX-License-Identifier: GPL-3.0-only

#include "ToolCatalog.h"

#include "system/Executables.h"
#include "system/Host.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>

namespace {

const QString jdkId = QStringLiteral("jdk");
const QString nodeId = QStringLiteral("node");

// The system's name in the download services' URLs.
QString downloadName(OperatingSystem system)
{
    switch (system) {
    case OperatingSystem::Windows:
        return QStringLiteral("windows");
    case OperatingSystem::MacOS:
        return QStringLiteral("mac");
    case OperatingSystem::Linux:
        return QStringLiteral("linux");
    }
    return {};
}

DownloadSource failure(const QString &error)
{
    DownloadSource source;
    source.error = error;
    return source;
}

bool isWindows(const Platform &platform)
{
    return platform.os == QLatin1String("windows");
}

ArchiveFormat archiveFormatOf(const Platform &platform)
{
    return isWindows(platform) ? ArchiveFormat::Zip : ArchiveFormat::TarGz;
}

QString archiveSuffixOf(const Platform &platform)
{
    return isWindows(platform) ? QStringLiteral(".zip") : QStringLiteral(".tar.gz");
}

DownloadSource jdkSource(const QString &version, const Platform &platform, const QByteArray &metadata)
{
    // OpenJDK17U-jdk_x64_linux_hotspot_17.0.20.1_1.tar.gz
    const QString prefix = QStringLiteral("OpenJDK%1U-jdk_%2_%3_hotspot_").arg(version, platform.arch, platform.os);
    const QString suffix = archiveSuffixOf(platform);
    for (const QJsonValue &asset : QJsonDocument::fromJson(metadata).object().value("assets").toArray()) {
        const QString name = asset.toObject().value("name").toString();
        if (!name.startsWith(prefix) || !name.endsWith(suffix))
            continue;
        DownloadSource source;
        source.fileName = name;
        source.url = QUrl(asset.toObject().value("browser_download_url").toString());
        source.checksumUrl = QUrl(source.url.toString() + QStringLiteral(".sha256.txt"));
        source.format = archiveFormatOf(platform);
        return source;
    }
    return failure(QObject::tr("No JDK %1 build for %2 %3").arg(version, platform.os, platform.arch));
}

// The newest release of a major version: index.json lists the newest first.
QString nodeRelease(const QString &version, const QByteArray &metadata)
{
    for (const QJsonValue &entry : QJsonDocument::fromJson(metadata).array()) {
        const QString candidate = entry.toObject().value("version").toString();
        if (candidate.startsWith(QStringLiteral("v%1.").arg(version)))
            return candidate;
    }
    return {};
}

QString nodeOsName(const Platform &platform)
{
    if (isWindows(platform))
        return QStringLiteral("win");
    if (platform.os == QLatin1String("mac"))
        return QStringLiteral("darwin");
    return QStringLiteral("linux");
}

DownloadSource nodeSource(const QString &version, const Platform &platform, const QByteArray &metadata)
{
    const QString release = nodeRelease(version, metadata);
    if (release.isEmpty())
        return failure(QObject::tr("No Node.js %1 release found").arg(version));
    const QString arch = platform.arch == QLatin1String("aarch64") ? QStringLiteral("arm64") : QStringLiteral("x64");
    DownloadSource source;
    source.fileName =
        QStringLiteral("node-%1-%2-%3%4").arg(release, nodeOsName(platform), arch, archiveSuffixOf(platform));
    source.url = QUrl(QStringLiteral("https://nodejs.org/dist/%1/%2").arg(release, source.fileName));
    source.checksumUrl = QUrl(QStringLiteral("https://nodejs.org/dist/%1/SHASUMS256.txt").arg(release));
    source.format = archiveFormatOf(platform);
    return source;
}

DownloadSource clangdSource(const QString &version, const Platform &platform)
{
    // Built for x64 only, on Linux and Windows; universal on macOS.
    if (platform.arch != QLatin1String("x64") && platform.os != QLatin1String("mac"))
        return failure(QObject::tr("clangd publishes no build for %1 %2: install it with your package manager")
                           .arg(platform.os, platform.arch));
    DownloadSource source;
    source.fileName = QStringLiteral("clangd-%1-%2.zip").arg(platform.os, version);
    source.url =
        QUrl(QStringLiteral("https://github.com/clangd/clangd/releases/download/%1/%2").arg(version, source.fileName));
    source.format = ArchiveFormat::Zip;
    return source;
}

// A server that is down may answer with a web page, and HTTP 200, in place
// of the file asked for: download.eclipse.org's "Temporary shutdown" does.
bool isWebPage(const QString &text)
{
    return text.startsWith(QLatin1String("<!DOCTYPE"), Qt::CaseInsensitive) ||
           text.startsWith(QLatin1String("<html"), Qt::CaseInsensitive);
}

DownloadSource jdtlsSource(const QString &version, const QByteArray &metadata)
{
    const QString name = QString::fromUtf8(metadata).trimmed();
    if (isWebPage(name))
        return failure(QObject::tr("download.eclipse.org answered with a web page instead of the name of the jdtls "
                                   "%1 milestone: the server may be down, retry later")
                           .arg(version));
    if (!name.startsWith(QLatin1String("jdt-language-server-")) || !name.endsWith(QLatin1String(".tar.gz")))
        return failure(QObject::tr("No jdtls %1 milestone found").arg(version));
    DownloadSource source;
    source.fileName = name;
    source.url = QUrl(QStringLiteral("https://download.eclipse.org/jdtls/milestones/%1/%2").arg(version, name));
    source.checksumUrl = QUrl(source.url.toString() + QStringLiteral(".sha256"));
    source.format = ArchiveFormat::TarGz;
    return source;
}

} // namespace

namespace ToolCatalog {

QList<ToolSpec> all()
{
    QList<ToolSpec> tools;

    ToolSpec jdk;
    jdk.id = jdkId;
    jdk.title = QStringLiteral("JDK");
    jdk.isRuntime = true;
    jdk.family = QStringLiteral("java");
    jdk.versions = {QStringLiteral("25"), QStringLiteral("21"), QStringLiteral("17"), QStringLiteral("11"),
                    QStringLiteral("8")};
    jdk.program = QStringLiteral("java");
    jdk.executables = {Executables::fileName(QStringLiteral("bin/java")), QStringLiteral("Contents/Home/bin/java")};
    tools.append(jdk);

    ToolSpec node;
    node.id = nodeId;
    node.title = QStringLiteral("Node.js");
    node.isRuntime = true;
    node.family = QStringLiteral("javascript");
    node.versions = {QStringLiteral("26"), QStringLiteral("24"), QStringLiteral("22"), QStringLiteral("20")};
    node.program = QStringLiteral("node");
    node.executables = {QStringLiteral("bin/node"), QStringLiteral("node.exe")};
    tools.append(node);

    ToolSpec clangd;
    clangd.id = QStringLiteral("clangd");
    clangd.title = QStringLiteral("clangd");
    clangd.family = QStringLiteral("cpp");
    clangd.versions = {QStringLiteral("23.1.0"), QStringLiteral("22.1.6")};
    clangd.program = QStringLiteral("clangd");
    clangd.executables = {Executables::fileName(QStringLiteral("bin/clangd"))};
    tools.append(clangd);

    ToolSpec jdtls;
    jdtls.id = QStringLiteral("jdtls");
    jdtls.title = QStringLiteral("Eclipse JDT Language Server");
    jdtls.family = QStringLiteral("java");
    jdtls.versions = {QStringLiteral("1.62.0"), QStringLiteral("1.61.0")};
    jdtls.program = QStringLiteral("jdtls");
    // Started with Java directly; the script tells where the install is.
    jdtls.executables = {QStringLiteral("bin/jdtls"), QStringLiteral("bin/jdtls.bat")};
    jdtls.runtime = jdkId;
    jdtls.minimumRuntime = 21;
    tools.append(jdtls);

    ToolSpec typescript;
    typescript.id = QStringLiteral("typescript-language-server");
    typescript.title = QStringLiteral("TypeScript Language Server");
    typescript.family = QStringLiteral("javascript");
    typescript.versions = {QStringLiteral("6.0.2")};
    typescript.program = QStringLiteral("typescript-language-server");
    typescript.executables = {QStringLiteral("node_modules/typescript-language-server/lib/cli.mjs")};
    typescript.script = true;
    typescript.runtime = nodeId;
    typescript.minimumRuntime = 22;
    // Its TypeScript when the project has none of its own (5.x: 7 has
    // no tsserver).
    typescript.npmPackages = {QStringLiteral("typescript-language-server@{version}"),
                              QStringLiteral("typescript@5.9.3")};
    tools.append(typescript);

    ToolSpec pyright;
    pyright.id = QStringLiteral("pyright");
    pyright.title = QStringLiteral("Pyright");
    pyright.family = QStringLiteral("python");
    pyright.versions = {QStringLiteral("1.1.414")};
    pyright.program = QStringLiteral("pyright-langserver");
    pyright.executables = {QStringLiteral("node_modules/pyright/langserver.index.js")};
    pyright.script = true;
    pyright.runtime = nodeId;
    pyright.minimumRuntime = 14;
    pyright.npmPackages = {QStringLiteral("pyright@{version}")};
    tools.append(pyright);

    return tools;
}

const ToolSpec *find(const QString &id)
{
    static const QList<ToolSpec> tools = all();
    for (const ToolSpec &tool : tools) {
        if (tool.id == id)
            return &tool;
    }
    return nullptr;
}

QStringList servers(const QString &family)
{
    QStringList ids;
    for (const ToolSpec &tool : all()) {
        if (!tool.isRuntime && tool.family == family)
            ids.append(tool.id);
    }
    // Known to LanguageServers, never downloaded.
    if (family == QLatin1String("python"))
        ids.append(QStringLiteral("pylsp"));
    return ids;
}

QString runtimeOf(const QString &family)
{
    if (family == QLatin1String("java"))
        return jdkId;
    if (family == QLatin1String("javascript"))
        return nodeId;
    return {};
}

QStringList runtimeVersions(const QString &family)
{
    if (family == QLatin1String("cpp"))
        return {QStringLiteral("c++26"), QStringLiteral("c++23"), QStringLiteral("c++20"),
                QStringLiteral("c++17"), QStringLiteral("c++14"), QStringLiteral("c++11")};
    if (const ToolSpec *runtime = find(runtimeOf(family)))
        return runtime->versions;
    return {};
}

QString defaultRuntimeVersion(const QString &family)
{
    if (family == QLatin1String("java"))
        return QStringLiteral("21");
    if (family == QLatin1String("javascript"))
        return QStringLiteral("24"); // the active LTS
    if (family == QLatin1String("cpp"))
        return QStringLiteral("c++20");
    return {};
}

Platform currentPlatform()
{
    Platform platform;
    platform.os = downloadName(Host::operatingSystem());
    const QString cpu = QSysInfo::currentCpuArchitecture();
    platform.arch = cpu.startsWith(QLatin1String("arm")) ? QStringLiteral("aarch64") : QStringLiteral("x64");
    return platform;
}

QUrl metadataUrl(const QString &id, const QString &version)
{
    if (id == jdkId) // Adoptium's Temurin builds, as GitHub releases
        return {
            QStringLiteral("https://api.github.com/repos/adoptium/temurin%1-binaries/releases/latest").arg(version)};
    if (id == nodeId) // to know the latest release of the major version
        return {QStringLiteral("https://nodejs.org/dist/index.json")};
    if (id == QLatin1String("jdtls")) // the name of the milestone's archive
        return {QStringLiteral("https://download.eclipse.org/jdtls/milestones/%1/latest.txt").arg(version)};
    return {};
}

DownloadSource source(const QString &id, const QString &version, const Platform &platform, const QByteArray &metadata)
{
    if (id == jdkId)
        return jdkSource(version, platform, metadata);
    if (id == nodeId)
        return nodeSource(version, platform, metadata);
    if (id == QLatin1String("clangd"))
        return clangdSource(version, platform);
    if (id == QLatin1String("jdtls"))
        return jdtlsSource(version, metadata);
    if (const ToolSpec *tool = find(id); tool && !tool->npmPackages.isEmpty()) {
        DownloadSource source;
        source.format = ArchiveFormat::Npm;
        return source;
    }
    return failure(QObject::tr("Kodamiko cannot download %1").arg(id));
}

QString checksumFor(const QByteArray &checksums, const QString &fileName)
{
    const QList<QByteArray> lines = checksums.trimmed().split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> fields = line.simplified().split(' ');
        const bool isHash = !fields.isEmpty() && (fields.first().size() == 64 || fields.first().size() == 128);
        if (!isHash)
            continue;
        // "hash", or "hash  name" ("*name" in binary mode).
        QByteArray name = fields.size() > 1 ? fields.at(1) : QByteArray();
        if (name.startsWith('*'))
            name.remove(0, 1);
        if ((lines.size() == 1 && fields.size() <= 2) || QString::fromUtf8(name) == fileName)
            return QString::fromLatin1(fields.first().toLower());
    }
    return {};
}

} // namespace ToolCatalog
