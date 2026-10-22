// SPDX-License-Identifier: GPL-3.0-only

#include "agent/vendor/claude/ClaudeTrust.h"
#include "projects/ProjectConfig.h"
#include "projects/ProjectDetector.h"
#include "projects/ProjectStore.h"
#include "recipes/RecipeLibrary.h"
#include "toolchains/InstallPlan.h"
#include "toolchains/Installer.h"
#include "toolchains/QtRepository.h"
#include "toolchains/ToolCatalog.h"
#include "toolchains/Toolchains.h"
#include "ui/Theme.h"
#include "ui/ThemeStyle.h"
#include "welcome/NewProjectForm.h"
#include "welcome/NewProjectPopup.h"
#include "welcome/ProjectSettingsPopup.h"
#include "welcome/ProjectSetupForm.h"
#include "welcome/ProjectSetupPanel.h"
#include "welcome/WelcomeWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {

void write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

// The executables ToolCatalog looks for in Duet's installs of a JDK and a Node.
#ifdef Q_OS_WIN
const QString javaExecutable = QStringLiteral("bin/java.exe");
#else
const QString javaExecutable = QStringLiteral("bin/java");
#endif
const QString nodeExecutable = QStringLiteral("bin/node");

// A tool installed by Duet, as Installer leaves it.
void fakeInstall(const Toolchains &toolchains, const QString &tool, const QString &version, const QString &executable)
{
    const QString folder = toolchains.installFolder(tool, version);
    write(folder + QLatin1Char('/') + executable, "#!/bin/sh\n");
    QFile::setPermissions(folder + QLatin1Char('/') + executable,
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    write(folder + QStringLiteral("/.complete"), "");
}

// The Updates.xml of a Qt release, cut down to a kit, an add-on and a
// kit of another processor.
const QByteArray qtUpdates = R"(<Updates>
 <PackageUpdate>
  <Name>qt.qt6.6103.win64_msvc2022_64</Name>
  <Version>6.10.3-0-202603310407</Version>
  <DownloadableArchives>qtbase-MSVC2022-X86_64.7z, d3dcompiler_47-x64.7z</DownloadableArchives>
  <UpdateFile CompressedSize="1000" OS="Any" UncompressedSize="5000"/>
  <Operations>
   <Operation name="Extract">
    <Argument>@TargetDir@/6.10.3/msvc2022_64</Argument>
    <Argument>qtbase-MSVC2022-X86_64.7z</Argument>
   </Operation>
   <Operation name="Extract">
    <Argument>@TargetDir@/6.10.3/msvc2022_64/bin</Argument>
    <Argument>d3dcompiler_47-x64.7z</Argument>
   </Operation>
  </Operations>
 </PackageUpdate>
 <PackageUpdate>
  <Name>qt.qt6.6103.win64_msvc2022_arm64_cross_compiled</Name>
  <Version>6.10.3-0-202603310407</Version>
  <DownloadableArchives>qtbase-MSVC2022-ARM64.7z</DownloadableArchives>
  <Operations>
   <Operation name="Extract">
    <Argument>@TargetDir@/6.10.3/msvc2022_arm64</Argument>
    <Argument>qtbase-MSVC2022-ARM64.7z</Argument>
   </Operation>
  </Operations>
 </PackageUpdate>
 <PackageUpdate>
  <Name>qt.qt6.6103.addons.qtcharts.win64_msvc2022_64</Name>
  <Version>6.10.3-0-202603310407</Version>
  <DownloadableArchives>qtcharts-MSVC2022-X86_64.7z</DownloadableArchives>
  <UpdateFile CompressedSize="200" OS="Any" UncompressedSize="900"/>
  <Operations>
   <Operation name="Extract">
    <Argument>@TargetDir@/6.10.3/msvc2022_64</Argument>
    <Argument>qtcharts-MSVC2022-X86_64.7z</Argument>
   </Operation>
  </Operations>
 </PackageUpdate>
</Updates>)";

const QtRepository::Release qt6103{.version = QVersionNumber(6, 10, 3), .folder = "qt6_6103"};

// A Qt kit, as Installer leaves it (`modules` in its marker), or as Qt's
// installer does (no marker) when `modules` is nullopt.
void fakeQt(const QString &folder, const std::optional<QStringList> &modules)
{
    write(folder + "/lib/cmake/Qt6/Qt6Config.cmake", "");
    if (modules)
        write(folder + "/.complete", (QStringList{"Qt"} + *modules).join('\n').toUtf8());
}

ProjectConfig javaProject(const QString &path)
{
    ProjectConfig project;
    project.path = path;
    project.name = "p";
    project.configured = true;
    project.languages.append({"java", true, "17", {}, "jdtls", "1.61.0"});
    return project;
}

// C++ with a clangd already downloaded: nothing to download.
ProjectConfig cppProject(const QString &path, const Toolchains &toolchains)
{
    fakeInstall(toolchains, "clangd", "23.1.0", "bin/clangd");
    ProjectConfig project;
    project.path = path;
    project.name = "p";
    project.languages.append({"cpp", true, "c++20", {}, "clangd", "23.1.0"});
    return project;
}

QStringList titles(const QList<InstallStep> &steps)
{
    QStringList list;
    for (const InstallStep &step : steps)
        list.append(step.title);
    return list;
}

} // namespace

class TestProjects : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // Settings of the tests, not the user's.
        QCoreApplication::setOrganizationName(QStringLiteral("DuetTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_projects"));
        QSettings().clear();
        // Claude's state of the tests, not the user's ~/.claude.json.
        qputenv("CLAUDE_CONFIG_DIR", claudeConfig_.path().toUtf8());
    }

    void cleanupTestCase() { qunsetenv("CLAUDE_CONFIG_DIR"); }

    void cleanup()
    {
        QSettings().clear();
        QFile::remove(claudeConfig_.filePath(".claude.json"));
    }

    void parsesJavaVersions()
    {
        QCOMPARE(ProjectDetector::javaVersionFromPom(
                     "<properties><maven.compiler.release>17</maven.compiler.release></properties>"),
                 QString("17"));
        QCOMPARE(ProjectDetector::javaVersionFromPom("<properties><java.version>1.8</java.version></properties>"),
                 QString("8"));
        QCOMPARE(ProjectDetector::javaVersionFromPom("<properties><jdk>21</jdk>"
                                                     "<maven.compiler.source>${jdk}</maven.compiler.source>"
                                                     "</properties>"),
                 QString("21"));
        QVERIFY(ProjectDetector::javaVersionFromPom("<project/>").isEmpty());
        QCOMPARE(ProjectDetector::javaMajor("17.0.2-tem"), QString("17"));
    }

    void parsesOtherVersions()
    {
        QCOMPARE(ProjectDetector::nodeMajor("v20.11.1"), QString("20"));
        QCOMPARE(ProjectDetector::nodeMajor(">=18"), QString("18"));
        QCOMPARE(ProjectDetector::nodeMajor("^22.1"), QString("22"));
        QVERIFY(ProjectDetector::nodeMajor("lts/*").isEmpty());
        QCOMPARE(ProjectDetector::nodeVersionFromPackageJson("{\"engines\": {\"node\": \">=20.0.0\"}}"), QString("20"));
        QCOMPARE(ProjectDetector::pythonMinor("3.12.1"), QString("3.12"));
        QCOMPARE(ProjectDetector::pythonVersionFromPyproject("[project]\nrequires-python = \">=3.10\"\n"),
                 QString("3.10"));
        QCOMPARE(ProjectDetector::cppStandardFromCMake("set(CMAKE_CXX_STANDARD 20)"), QString("c++20"));
        QCOMPARE(ProjectDetector::cppStandardFromCMake("target_compile_features(x PUBLIC cxx_std_17)"),
                 QString("c++17"));
    }

    void detectsTheLanguagesOfAFolder()
    {
        const QTemporaryDir dir;
        write(dir.filePath("backend/pom.xml"), "<maven.compiler.release>17</maven.compiler.release>");
        write(dir.filePath("web/package.json"), "{}");
        write(dir.filePath(".nvmrc"), "v20.11.0\n");
        write(dir.filePath("CMakeLists.txt"), "set(CMAKE_CXX_STANDARD 23)");
        // Dependencies do not count.
        for (int i = 0; i < 5; ++i)
            write(dir.filePath(QStringLiteral("node_modules/x/f%1.py").arg(i)), "");
        // A stray script is not a Python project.
        write(dir.filePath("tools/one.py"), "");

        const QList<DetectedLanguage> found = ProjectDetector::detect(dir.path());
        QStringList languages;
        for (const DetectedLanguage &language : found)
            languages.append(language.language);
        QCOMPARE(languages, (QStringList{"java", "javascript", "cpp"}));
        QCOMPARE(found[0].runtimeVersion, QString("17"));
        QCOMPARE(found[0].evidence, QString("backend/pom.xml"));
        QCOMPARE(found[1].runtimeVersion, QString("20"));
        QCOMPARE(found[2].runtimeVersion, QString("c++23"));
    }

    void findsAPythonEnvironment()
    {
        const QTemporaryDir dir;
        write(dir.filePath("pyproject.toml"), "requires-python = \">=3.9\"");
        write(dir.filePath(".venv/pyvenv.cfg"), "home = /usr/bin\nversion = 3.12.3\n");
#ifdef Q_OS_WIN
        write(dir.filePath(".venv/Scripts/python.exe"), "");
#else
        write(dir.filePath(".venv/bin/python"), "");
        QFile::setPermissions(dir.filePath(".venv/bin/python"), QFile::ReadOwner | QFile::ExeOwner);
#endif
        const QList<DetectedLanguage> found = ProjectDetector::detect(dir.path());
        QCOMPARE(found.size(), 1);
        QCOMPARE(found[0].language, QString("python"));
        // The environment's version wins over the minimum of pyproject.
        QCOMPARE(found[0].runtimeVersion, QString("3.12"));
        QVERIFY(found[0].runtimePath.contains(".venv"));
    }

    void storesProjects()
    {
        QSettings().setValue("recentFolders", QStringList{"/work/b", "/work/a"});
        QList<ProjectConfig> projects = ProjectStore::all();
        QCOMPARE(projects.size(), 2); // the former recent folders
        QCOMPARE(projects[0].path, QString("/work/b"));
        QVERIFY(!projects[0].configured);

        ProjectConfig config = ProjectStore::touch("/work/a");
        config.configured = true;
        config.languages.append({"java", true, "17", {}, "jdtls", "1.62.0"});
        ProjectStore::save(config);
        projects = ProjectStore::all();
        QCOMPARE(projects[0].path, QString("/work/a")); // opened last
        QCOMPARE(projects[0].name, QString("a"));
        QVERIFY(projects[0].configured);
        QCOMPARE(projects[0].languages, config.languages);

        ProjectStore::remove("/work/b");
        QCOMPARE(ProjectStore::all().size(), 1);
        QVERIFY(!ProjectStore::find("/work/b"));
    }

    void startsInTheRequestedFolderOverTheLastOne()
    {
        QSettings().setValue("lastFolder", "/work/last");

        QCOMPARE(ProjectStore::startFolder("/work/requested"), QString("/work/requested"));
    }

    void startsInTheLastFolderWhenNoneIsRequested()
    {
        QSettings().setValue("lastFolder", "/work/last");

        QCOMPARE(ProjectStore::startFolder({}), QString("/work/last"));
    }

    void startsInTheHomeFolderWithoutALastOne() { QCOMPARE(ProjectStore::startFolder({}), QDir::homePath()); }

    void startsInTheHomeFolderWhenTheLastOneIsEmpty()
    {
        QSettings().setValue("lastFolder", "");

        QCOMPARE(ProjectStore::startFolder({}), QDir::homePath());
    }

    void namesTheDownloads()
    {
        const Platform linuxX64{"linux", "x64"};
        const QByteArray release = "{\"assets\": ["
                                   "{\"name\": \"OpenJDK17U-jdk_aarch64_linux_hotspot_17.0.20.1_1.tar.gz\", "
                                   "\"browser_download_url\": \"https://x/arm.tar.gz\"},"
                                   "{\"name\": \"OpenJDK17U-jdk_x64_linux_hotspot_17.0.20.1_1.tar.gz\", "
                                   "\"browser_download_url\": \"https://x/jdk.tar.gz\"},"
                                   "{\"name\": \"OpenJDK17U-jdk_x64_windows_hotspot_17.0.20.1_1.zip\", "
                                   "\"browser_download_url\": \"https://x/jdk.zip\"}]}";
        DownloadSource jdk = ToolCatalog::source("jdk", "17", linuxX64, release);
        QCOMPARE(jdk.url, QUrl("https://x/jdk.tar.gz"));
        QCOMPARE(jdk.checksumUrl, QUrl("https://x/jdk.tar.gz.sha256.txt"));
        QCOMPARE(jdk.format, ArchiveFormat::TarGz);
        jdk = ToolCatalog::source("jdk", "17", {"windows", "x64"}, release);
        QCOMPARE(jdk.url, QUrl("https://x/jdk.zip"));
        QCOMPARE(jdk.format, ArchiveFormat::Zip);
        QVERIFY(!ToolCatalog::source("jdk", "17", {"mac", "aarch64"}, release).error.isEmpty());
        QCOMPARE(ToolCatalog::metadataUrl("jdk", "21"),
                 QUrl("https://api.github.com/repos/adoptium/temurin21-binaries/releases/latest"));

        const QByteArray index =
            "[{\"version\": \"v26.1.0\"}, {\"version\": \"v24.22.0\"}, {\"version\": \"v24.21.0\"}]";
        const DownloadSource node = ToolCatalog::source("node", "24", {"mac", "aarch64"}, index);
        QCOMPARE(node.url, QUrl("https://nodejs.org/dist/v24.22.0/node-v24.22.0-darwin-arm64.tar.gz"));
        QCOMPARE(node.checksumUrl, QUrl("https://nodejs.org/dist/v24.22.0/SHASUMS256.txt"));

        QCOMPARE(ToolCatalog::source("clangd", "23.1.0", linuxX64, {}).url,
                 QUrl("https://github.com/clangd/clangd/releases/download/23.1.0/clangd-linux-23.1.0.zip"));
        QVERIFY(!ToolCatalog::source("clangd", "23.1.0", {"linux", "aarch64"}, {}).error.isEmpty());
        QCOMPARE(
            ToolCatalog::source("jdtls", "1.62.0", linuxX64, "jdt-language-server-1.62.0-202610131109.tar.gz\n").url,
            QUrl("https://download.eclipse.org/jdtls/milestones/1.62.0/"
                 "jdt-language-server-1.62.0-202610131109.tar.gz"));
        QCOMPARE(ToolCatalog::source("pyright", "1.1.414", linuxX64, {}).format, ArchiveFormat::Npm);

        const QString hash(64, QLatin1Char('a'));
        QCOMPARE(ToolCatalog::checksumFor(hash.toLatin1() + "  jdk.tar.gz\n", "jdk.tar.gz"), hash);
        QCOMPARE(ToolCatalog::checksumFor(hash.toLatin1() + "\n", "anything"), hash);
        QCOMPARE(ToolCatalog::checksumFor(QString(64, 'b').toLatin1() + "  other.zip\n" + hash.toLatin1() +
                                              "  node.tar.gz\n",
                                          "node.tar.gz"),
                 hash);
        QVERIFY(ToolCatalog::checksumFor(hash.toLatin1() + "  a\n" + hash.toLatin1() + "  b\n", "c").isEmpty());
    }

    void saysTheServerMayBeDownWhenItAnswersAJdtlsMilestoneWithAWebPage()
    {
        const QByteArray shutdownPage = "<!DOCTYPE html>\n<html><head><title>Temporary shutdown.</title></head></html>";

        const DownloadSource jdtls = ToolCatalog::source("jdtls", "1.62.0", {"linux", "x64"}, shutdownPage);

        QVERIFY(jdtls.url.isEmpty());
        QVERIFY2(jdtls.error.contains("server may be down"), qPrintable(jdtls.error));
    }

    void legacyDownloadsMoveToTheStoreWithoutReplacingItsOwn()
    {
        const QTemporaryDir dir;
        const Toolchains legacy(dir.filePath("legacy"), false);
        const Toolchains store(dir.filePath("store"), false);
        fakeInstall(legacy, "jdk", "21", "bin/java");
        fakeInstall(legacy, "clangd", "23.1.0", "bin/clangd");
        write(dir.filePath("legacy/.staging/node-24/node"), "");
        fakeInstall(store, "clangd", "23.1.0", "bin/clangd-of-the-store");

        const QStringList unmoved = LegacyToolchains::moveInto(legacy.root(), store.root());

        QVERIFY(unmoved.isEmpty());
        QVERIFY(store.isInstalled("jdk", "21"));
        const QString storeClangd = store.installFolder("clangd", "23.1.0");
        QVERIFY(QFileInfo::exists(storeClangd + "/bin/clangd-of-the-store"));
        QVERIFY(!QFileInfo::exists(storeClangd + "/bin/clangd"));
        QVERIFY(!QFileInfo::exists(store.root() + "/.staging"));
        QVERIFY(!QFileInfo::exists(legacy.root()));
    }

    void plansTheRuntimesTheRecipeRequires()
    {
        const QTemporaryDir dir;
        const Toolchains toolchains(dir.path(), false);
        const ProjectConfig config;
        const QList<ToolRequirement> required{{.tool = "node", .version = "24", .kit = {}, .modules = {}},
                                              {.tool = "jdk", .version = {}, .kit = {}, .modules = {}}};

        QCOMPARE(titles(InstallPlan::plan(config, toolchains, required)),
                 (QStringList{"Node.js 24", "JDK " + ToolCatalog::defaultRuntimeVersion("java")}));

        fakeInstall(toolchains, "node", "24", nodeExecutable);
        fakeInstall(toolchains, "jdk", "17", javaExecutable);
        QVERIFY(InstallPlan::plan(config, toolchains, required).isEmpty()); // any JDK will do
    }

    void saysWhatARecipeRequiresThatDuetCannotDownload()
    {
        const QTemporaryDir dir;
        const Toolchains toolchains(dir.path(), false);
        const QList<ToolRequirement> required{{.tool = "maven", .version = "3", .kit = {}, .modules = {}},
                                              {.tool = "node", .version = "7", .kit = {}, .modules = {}}};

        const QStringList problems = InstallPlan::unavailable({}, toolchains, required);

        QVERIFY(InstallPlan::plan({}, toolchains, required).isEmpty());
        QCOMPARE(problems.size(), 2);
        QVERIFY(problems.first().contains("maven")); // not in the PATH: Toolchains does not look
        QVERIFY(problems.last().contains("node 7"));
    }

    void choosesTheNewestQtReleaseOfTheVersionAsked()
    {
        const QByteArray listing = R"(<a href="qt6_683/">x</a> <a href="qt6_6103/">x</a> <a href="qt6_6111/">x</a>
            <a href="qt6_6120/">x</a> <a href="qt6_6103_src_doc_examples/">x</a> <a href="qt6_62_preview/">x</a>)";

        QCOMPARE(QtRepository::newestRelease(listing, "6.10")->folder, QString("qt6_6103"));
        QCOMPARE(QtRepository::newestRelease(listing, "6.11")->version, QVersionNumber(6, 11, 1));
        QCOMPARE(QtRepository::newestRelease(listing, "")->version, QVersionNumber(6, 12, 0));
        QVERIFY(!QtRepository::newestRelease(listing, "6.9"));
    }

    void findsTheRepositoryOfAKit()
    {
        const QtRepository::Release qt6120{.version = QVersionNumber(6, 12, 0), .folder = "qt6_6120"};
        const QByteArray perKit = R"(<a href="qt6_6120_msvc2022_64/">x</a> <a href="qt6_6120_mingw/">x</a>)";

        QCOMPARE(QtRepository::repositoryFolder(perKit, qt6120, "msvc2022_64"), QString("qt6_6120_msvc2022_64"));
        QCOMPARE(QtRepository::repositoryFolder(R"(<a href="qt6_6103/">x</a>)", qt6103, "msvc2022_64"),
                 QString("qt6_6103"));
        QVERIFY(QtRepository::repositoryFolder(perKit, qt6120, "gcc_64").isEmpty());
    }

    void readsTheArchivesOfAQtKitAndItsModules()
    {
        const QUrl repository("https://download.qt.io/r");
        QString error;

        const std::optional<QtPackageSet> set =
            QtRepository::packages(qtUpdates, repository, qt6103, "msvc2022_64", {"qtcharts"}, &error);

        QVERIFY2(set, qPrintable(error));
        QStringList folders;
        for (const QtArchive &archive : set->archives)
            folders.append(archive.fileName + " → " + archive.folder);
        QCOMPARE(folders, (QStringList{"qtbase-MSVC2022-X86_64.7z → ", "d3dcompiler_47-x64.7z → bin",
                                       "qtcharts-MSVC2022-X86_64.7z → "}));
        const QString base = "https://download.qt.io/r/qt.qt6.6103.win64_msvc2022_64/6.10.3-0-202603310407";
        QCOMPARE(set->archives.first().url, QUrl(base + "qtbase-MSVC2022-X86_64.7z"));
        QCOMPARE(set->archives.first().checksumUrl, QUrl(base + "qtbase-MSVC2022-X86_64.7z.sha1"));
        QCOMPARE(set->compressedSize, 1200);
    }

    void saysWhichQtKitOrModuleTheRepositoryLacks()
    {
        const QUrl repository("https://download.qt.io/r");
        QString error;

        QVERIFY(!QtRepository::packages(qtUpdates, repository, qt6103, "gcc_64", {}, &error));
        QVERIFY(error.contains("gcc_64"));
        QVERIFY(!QtRepository::packages(qtUpdates, repository, qt6103, "msvc2022_64", {"qtgraphs"}, &error));
        QVERIFY(error.contains("qtgraphs"));
    }

    void findsTheNewestQtOfAKitWithItsModules()
    {
        const QTemporaryDir dir;
        const Toolchains toolchains(dir.path(), false);
        fakeQt(toolchains.qtInstallFolder("6.8.3", "msvc2022_64"), QStringList{"qtcharts"});
        fakeQt(toolchains.qtInstallFolder("6.10.3", "msvc2022_64"), QStringList{});
        fakeQt(toolchains.qtInstallFolder("6.11.0", "mingw_64"), QStringList{});
        QDir().mkpath(toolchains.qtInstallFolder("7.0.0", "msvc2022_64")); // without Qt6Config.cmake

        QCOMPARE(toolchains.qtFolder("", "msvc2022_64"), toolchains.qtInstallFolder("6.10.3", "msvc2022_64"));
        QCOMPARE(toolchains.qtFolder("6.8", "msvc2022_64"), toolchains.qtInstallFolder("6.8.3", "msvc2022_64"));
        QCOMPARE(toolchains.qtFolder("", "msvc2022_64", {"qtcharts"}),
                 toolchains.qtInstallFolder("6.8.3", "msvc2022_64"));
        QVERIFY(toolchains.qtFolder("6.9", "msvc2022_64").isEmpty());
        QVERIFY(toolchains.qtFolder("", "gcc_64").isEmpty());
    }

    void aQtInstalledWithoutDuetIsTakenWithItsModules()
    {
        const QTemporaryDir dir;
        const Toolchains toolchains(dir.path(), false);
        fakeQt(toolchains.qtInstallFolder("6.10.3", "msvc2022_64"), std::nullopt);

        QCOMPARE(toolchains.qtFolder("6.10", "msvc2022_64", {"qtcharts"}),
                 toolchains.qtInstallFolder("6.10.3", "msvc2022_64"));
    }

    void plansTheQtKitTheRecipeRequires()
    {
        const QTemporaryDir dir;
        const Toolchains toolchains(dir.path(), false);
        const QList<ToolRequirement> required{
            {.tool = "qt", .version = "6.10", .kit = "msvc2022_64", .modules = {"qtcharts"}}};

        const QList<InstallStep> steps = InstallPlan::plan({}, toolchains, required);

        QCOMPARE(titles(steps), QStringList{"Qt 6.10 msvc2022_64 with qtcharts"});
        QCOMPARE(steps.first().kit, QString("msvc2022_64"));
        QCOMPARE(steps.first().modules, QStringList{"qtcharts"});
        fakeQt(toolchains.qtInstallFolder("6.10.3", "msvc2022_64"), QStringList{"qtcharts"});
        QVERIFY(InstallPlan::plan({}, toolchains, required).isEmpty());
    }

    void aRequiredQtWithoutAKitIsAProblem()
    {
        const QTemporaryDir dir;
        const Toolchains toolchains(dir.path(), false);
        const QList<ToolRequirement> required{{.tool = "qt", .version = "6.10", .kit = {}, .modules = {}}};

        QVERIFY(InstallPlan::plan({}, toolchains, required).isEmpty());
        QCOMPARE(InstallPlan::unavailable({}, toolchains, required).size(), 1);
    }

    void aProgramOfThePathMeetsARequirement()
    {
        const QTemporaryDir dir;
        const Toolchains toolchains(dir.path(), true);
#ifdef Q_OS_WIN
        const QString program = "cmd";
#else
        const QString program = "sh";
#endif
        const QList<ToolRequirement> present{{.tool = program, .version = {}, .kit = {}, .modules = {}}};
        const QList<ToolRequirement> absent{
            {.tool = "no-such-program-of-duet", .version = {}, .kit = {}, .modules = {}}};

        QVERIFY(InstallPlan::unavailable({}, toolchains, present).isEmpty());
        QVERIFY(InstallPlan::plan({}, toolchains, absent).isEmpty()); // never downloaded
        QCOMPARE(InstallPlan::unavailable({}, toolchains, absent).size(), 1);
    }

    void plansTheDownloadsTheRecipeDescribes()
    {
        const QTemporaryDir dir;
        const Toolchains toolchains(dir.path(), false);
        const DownloadSource zip{.url = QUrl("https://example.org/maven.zip"),
                                 .fileName = "maven.zip",
                                 .checksumUrl = {},
                                 .checksum = "ab",
                                 .algorithm = ChecksumAlgorithm::Sha256,
                                 .format = ArchiveFormat::Zip,
                                 .error = {}};
        const QList<ToolRequirement> required{{.tool = "maven",
                                               .version = "3.9.16",
                                               .kit = {},
                                               .modules = {},
                                               .program = "mvn",
                                               .download = zip,
                                               .check = "bin/mvn"}};

        const QList<InstallStep> steps = InstallPlan::plan({}, toolchains, required);

        QCOMPARE(titles(steps), QStringList{"maven 3.9.16"});
        QCOMPARE(steps.first().source, std::optional(zip));
        QCOMPARE(steps.first().check, QString("bin/mvn"));
        QVERIFY(InstallPlan::unavailable({}, toolchains, required).isEmpty());
        write(toolchains.installFolder("maven", "3.9.16") + "/.complete", "maven 3.9.16");
        QVERIFY(InstallPlan::plan({}, toolchains, required).isEmpty());
    }

    void aProgramOfThePathSparesTheDownload()
    {
        const QTemporaryDir dir;
        const Toolchains toolchains(dir.path(), true);
#ifdef Q_OS_WIN
        const QString program = "cmd";
#else
        const QString program = "sh";
#endif
        const QList<ToolRequirement> required{{.tool = "shell",
                                               .version = "1",
                                               .kit = {},
                                               .modules = {},
                                               .program = program,
                                               .download = DownloadSource{},
                                               .check = {}}};

        QVERIFY(InstallPlan::plan({}, toolchains, required).isEmpty());
    }

    void installsADownloadTheRecipeDescribes()
    {
        if (QStandardPaths::findExecutable("tar").isEmpty())
            QSKIP("No tar");
        QTemporaryDir dir;
        // apache-maven-3.9.16/bin/mvn, as Maven's archive holds it.
        write(dir.filePath("source/apache-maven-3.9.16/bin/mvn"), "#!/bin/sh\necho maven\n");
        QProcess tar;
        tar.start("tar", {"-czf", dir.filePath("maven.tar.gz"), "-C", dir.filePath("source"), "apache-maven-3.9.16"});
        QVERIFY(tar.waitForFinished());
        QCOMPARE(tar.exitCode(), 0);
        QFile archive(dir.filePath("maven.tar.gz"));
        QVERIFY(archive.open(QIODevice::ReadOnly));
        const QString sha512 =
            QString::fromLatin1(QCryptographicHash::hash(archive.readAll(), QCryptographicHash::Sha512).toHex());
        Toolchains toolchains(dir.filePath("toolchains"), false);
        Installer installer(&toolchains);
        const DownloadSource source{.url = QUrl::fromLocalFile(dir.filePath("maven.tar.gz")),
                                    .fileName = "maven.tar.gz",
                                    .checksumUrl = {},
                                    .checksum = sha512,
                                    .algorithm = ChecksumAlgorithm::Sha512,
                                    .format = ArchiveFormat::TarGz,
                                    .error = {}};
        const InstallStep step{.tool = "maven",
                               .version = "3.9.16",
                               .title = "maven 3.9.16",
                               .reason = {},
                               .kit = {},
                               .modules = {},
                               .source = source,
                               .check = "bin/mvn"};
        QSignalSpy finished(&installer, &Installer::finished);
        QSignalSpy steps(&installer, &Installer::stepFinished);

        installer.run({step});

        QVERIFY(finished.wait(10000));
        QCOMPARE(steps.first().at(1).toString(), QString());
        QVERIFY(toolchains.isInstalled("maven", "3.9.16"));
        QVERIFY(QFileInfo::exists(toolchains.installFolder("maven", "3.9.16") + "/bin/mvn"));
    }

    void aDownloadWithoutItsCheckFileIsNotInstalled()
    {
        if (QStandardPaths::findExecutable("tar").isEmpty())
            QSKIP("No tar");
        QTemporaryDir dir;
        write(dir.filePath("source/maven/README"), "no bin/mvn\n");
        QProcess tar;
        tar.start("tar", {"-czf", dir.filePath("maven.tar.gz"), "-C", dir.filePath("source"), "maven"});
        QVERIFY(tar.waitForFinished());
        Toolchains toolchains(dir.filePath("toolchains"), false);
        Installer installer(&toolchains);
        const DownloadSource source{.url = QUrl::fromLocalFile(dir.filePath("maven.tar.gz")),
                                    .fileName = "maven.tar.gz",
                                    .checksumUrl = {},
                                    .checksum = {},
                                    .algorithm = ChecksumAlgorithm::Sha256,
                                    .format = ArchiveFormat::TarGz,
                                    .error = {}};
        QSignalSpy finished(&installer, &Installer::finished);
        QSignalSpy steps(&installer, &Installer::stepFinished);

        installer.run({{.tool = "maven",
                        .version = "3.9.16",
                        .title = "maven 3.9.16",
                        .reason = {},
                        .kit = {},
                        .modules = {},
                        .source = source,
                        .check = "bin/mvn"}});

        QVERIFY(finished.wait(10000));
        QVERIFY(steps.first().at(1).toString().contains("expected files"));
        QVERIFY(!toolchains.isInstalled("maven", "3.9.16"));
    }

    void plansWhatIsMissing()
    {
        const QTemporaryDir dir;
        const Toolchains toolchains(dir.path(), false);
        ProjectConfig config;
        config.languages.append({"java", true, "17", {}, "jdtls", "1.62.0"});
        // jdtls itself runs on Java 21.
        QCOMPARE(titles(InstallPlan::plan(config, toolchains, {})), (QStringList{"JDK 17", "JDK 21", "jdtls 1.62.0"}));

        fakeInstall(toolchains, "jdk", "21", "bin/java");
        fakeInstall(toolchains, "jdtls", "1.62.0", "bin/jdtls");
        QCOMPARE(titles(InstallPlan::plan(config, toolchains, {})), (QStringList{"JDK 17"}));
        config.languages[0].runtimeVersion = "21";
        QVERIFY(InstallPlan::plan(config, toolchains, {}).isEmpty());

        // A server needing Node brings one; disabled languages need nothing.
        config.languages.append({"python", true, "3.12", {}, "pyright", "1.1.414"});
        config.languages.append({"cpp", false, "c++20", {}, "clangd", "23.1.0"});
        QCOMPARE(titles(InstallPlan::plan(config, toolchains, {})), (QStringList{"Node.js 24", "pyright 1.1.414"}));
        QVERIFY(InstallPlan::unavailable(config, toolchains, {}).join(' ').contains("Python 3.12"));
    }

    void completesASetup()
    {
        const QTemporaryDir dir;
        const Toolchains toolchains(dir.path(), false);
        LanguageSetup setup;
        setup.language = "java";
        toolchains.complete(setup);
        QCOMPARE(setup.runtimeVersion, QString("21"));
        QCOMPARE(setup.server, QString("jdtls"));
        QCOMPARE(setup.serverVersion, QString("1.62.0")); // nothing installed: the newest
        fakeInstall(toolchains, "jdk", "17", "bin/java");
        const std::optional<RuntimeInstall> jdk = toolchains.runtime("jdk", "17");
        QVERIFY(jdk);
        QVERIFY(jdk->managed);
        QCOMPARE(jdk->home, toolchains.installFolder("jdk", "17"));
        QVERIFY(!toolchains.runtime("jdk", "11"));
        QCOMPARE(toolchains.runtimeAtLeast("jdk", 11)->version, QString("17"));
    }

#ifdef _MSC_VER
// C4702: on Windows the QSKIP returns, so cl calls the rest of the test unreachable.
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
    void installsAnArchive()
    {
#ifdef Q_OS_WIN
        QSKIP("Builds its archive with tar and sh");
#endif
        if (QStandardPaths::findExecutable("tar").isEmpty())
            QSKIP("No tar");
        QTemporaryDir dir;
        // clangd_23.1.0/bin/clangd, as the release's archive holds it.
        write(dir.filePath("source/clangd_23.1.0/bin/clangd"), "#!/bin/sh\necho clangd\n");
        QProcess tar;
        tar.start("tar", {"-czf", dir.filePath("clangd.tar.gz"), "-C", dir.filePath("source"), "clangd_23.1.0"});
        QVERIFY(tar.waitForFinished());
        QCOMPARE(tar.exitCode(), 0);
        QFile archive(dir.filePath("clangd.tar.gz"));
        QVERIFY(archive.open(QIODevice::ReadOnly));
        const QByteArray hash = QCryptographicHash::hash(archive.readAll(), QCryptographicHash::Sha256).toHex();
        write(dir.filePath("good.sha256"), hash + "  clangd.tar.gz\n");
        write(dir.filePath("bad.sha256"), QByteArray(64, '0') + "  clangd.tar.gz\n");

        Toolchains toolchains(dir.filePath("toolchains"), false);
        Installer installer(&toolchains);
        QString checksum = "good.sha256";
        installer.setSourceResolver([&](const InstallStep &) {
            DownloadSource source;
            source.url = QUrl::fromLocalFile(dir.filePath("clangd.tar.gz"));
            source.fileName = "clangd.tar.gz";
            source.checksumUrl = QUrl::fromLocalFile(dir.filePath(checksum));
            source.format = ArchiveFormat::TarGz;
            return source;
        });

        // A damaged download is not installed.
        checksum = "bad.sha256";
        QSignalSpy finished(&installer, &Installer::finished);
        QSignalSpy steps(&installer, &Installer::stepFinished);
        installer.run({{"clangd", "23.1.0", "clangd 23.1.0", {}}});
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.first().first().toBool(), false);
        QVERIFY(steps.first().at(1).toString().contains("checksum"));
        QVERIFY(!toolchains.isInstalled("clangd", "23.1.0"));

        checksum = "good.sha256";
        finished.clear();
        steps.clear();
        installer.run({{"clangd", "23.1.0", "clangd 23.1.0", {}}});
        QVERIFY(finished.wait(10000));
        QCOMPARE(steps.first().at(1).toString(), QString());
        QCOMPARE(finished.first().first().toBool(), true);
        QVERIFY(toolchains.isInstalled("clangd", "23.1.0"));
        // The single top folder is removed.
        const QString clangd = toolchains.installedExecutable("clangd", "23.1.0");
        QCOMPARE(clangd, toolchains.installFolder("clangd", "23.1.0") + "/bin/clangd");
        QVERIFY(QFileInfo(clangd).isExecutable());
        QCOMPARE(toolchains.serverExecutable("clangd", "23.1.0"), clangd);
        QVERIFY(!QDir(dir.filePath("toolchains/.staging")).exists());
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif

    void editsASetup()
    {
        const QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        ProjectSetupForm form(&toolchains);
        ProjectConfig project;
        project.path = dir.path();
        project.name = "p";
        project.configured = true;
        project.languages.append({"java", true, "17", {}, "jdtls", "1.61.0"});
        form.setProject(project, {});
        const ProjectConfig shown = form.project();
        // The languages the project does not use are left out.
        QCOMPARE(shown.languages.size(), 1);
        QCOMPARE(shown.languages.first(), project.languages.first());
        QVERIFY(shown.configured);
    }

    void setupListsTheUsedLanguagesFirstAndTheOthersOff()
    {
        const QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        ProjectSetupForm form(&toolchains);

        form.setProject(javaProject(dir.path()), {});

        QCOMPARE(form.languageList()->item(0)->text(), QString("Java"));
        for (int row = 1; row < form.languageList()->count(); ++row)
            QVERIFY(form.languageList()->item(row)->text().endsWith(" · Off"));
        QCOMPARE(form.currentLanguage(), QString("java"));
    }

    void setupShowsTheSelectedLanguageAlone()
    {
        const QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        ProjectSetupForm form(&toolchains);
        form.setProject(javaProject(dir.path()), {});

        form.languageList()->setCurrentRow(form.languageList()->count() - 1);

        const QString last = form.languageList()->currentItem()->text();
        QVERIFY(last.startsWith(ProjectLanguages::title(form.currentLanguage())));
    }

    void setupListsShowTheValueAndItsStateUnder()
    {
        const QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        ProjectSetupForm form(&toolchains);
        form.resize(860, 560);
        form.show();

        form.setProject(javaProject(dir.path()), {});

        const QList<QComboBox *> combos = form.findChildren<QComboBox *>();
        const auto jdk =
            std::ranges::find_if(combos, [](const QComboBox *combo) { return combo->currentText() == "JDK 17"; });
        QVERIFY(jdk != combos.end());
        const auto *state = (*jdk)->parentWidget()->findChild<QLabel *>("settingState");
        QCOMPARE(state->text(), QString("Will download"));
    }

    void turningALanguageOffMarksItOffWhereItIs()
    {
        const QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        ProjectSetupForm form(&toolchains);
        form.setProject(javaProject(dir.path()), {});
        const QList<QCheckBox *> switches = form.findChildren<QCheckBox *>();
        const auto java = std::ranges::find_if(switches, [](const QCheckBox *box) { return box->isChecked(); });
        QVERIFY(java != switches.end());

        (*java)->setChecked(false);

        QCOMPARE(form.languageList()->item(0)->text(), QString("Java · Off"));
        QVERIFY(!form.project().languages.first().enabled);
    }

    void setupOffersOnlyTheRecipesOfTheLanguage()
    {
        const QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        ProjectSetupForm form(&toolchains);

        form.setProject(javaProject(dir.path()), {});

        const QComboBox *recipes = form.recipeChoice("java");
        QCOMPARE(recipes->itemData(0).toString(), QString()); // None
        QVERIFY(recipes->findData("java-maven") > 0);
        QCOMPARE(recipes->findData("cpp-cmake"), -1);
        QCOMPARE(form.recipeChoice("cpp")->findData("java-maven"), -1);
    }

    void setupAttachesTheChosenRecipeToItsLanguage()
    {
        const QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        ProjectSetupForm form(&toolchains);
        form.setProject(javaProject(dir.path()), {});
        QComboBox *recipes = form.recipeChoice("java");

        recipes->setCurrentIndex(recipes->findData("java-maven"));

        QCOMPARE(form.project().language("java")->recipe, QString("java-maven"));
        QCOMPARE(form.project().recipes(), QStringList{"java-maven"});
    }

    void detectionKeepsTheRecipeOfALanguageNoFileShowsYet()
    {
        const QTemporaryDir dir;
        const Toolchains toolchains(dir.filePath("toolchains"), false);
        ProjectConfig saved;
        saved.path = dir.path();
        LanguageSetup cpp;
        cpp.language = "cpp";
        cpp.recipe = "cpp-cmake";
        saved.languages.append(cpp);

        const ProjectConfig detected = ProjectSetupForm::detectedProject(saved, toolchains);

        QVERIFY(detected.language("cpp"));
        QCOMPARE(detected.language("cpp")->recipe, QString("cpp-cmake"));
    }

    void aProjectHasTheRecipesOfItsEnabledLanguagesOnly()
    {
        ProjectConfig project;
        LanguageSetup java{"java", true, "21", {}, "jdtls", "1.62.0", "java-maven"};
        LanguageSetup cpp{"cpp", false, "c++20", {}, "clangd", "23.1.0", "cpp-cmake"};
        project.languages = {java, cpp};

        const ProjectConfig saved = ProjectConfig::fromVariant("/p", project.toVariant());

        QCOMPARE(saved.recipes(), QStringList{"java-maven"});
        QCOMPARE(saved.language("cpp")->recipe, QString("cpp-cmake"));
    }

    void aProjectIsReadBackAsItWasSaved()
    {
        ProjectConfig project;
        project.path = "/work/demo";
        project.name = "demo";
        project.lastOpened = QDateTime(QDate(2026, 10, 22), QTime(9, 30));
        project.configured = true;
        project.languages = {{"python", false, "3.12", "/usr/bin/python3", "pyright", "system", "python-uv"}};
        project.agentCheckCommand = "make check";
        project.needsRecipeSetup = true;

        const ProjectConfig saved = ProjectConfig::fromVariant(project.path, project.toVariant());

        QCOMPARE(saved.name, project.name);
        QCOMPARE(saved.lastOpened, project.lastOpened);
        QCOMPARE(saved.configured, project.configured);
        QCOMPARE(saved.languages, project.languages);
        QCOMPARE(saved.agentCheckCommand, project.agentCheckCommand);
        QCOMPARE(saved.needsRecipeSetup, project.needsRecipeSetup);
    }

    void groupsLanguagesIntoTheirFamilies()
    {
        QCOMPARE(ProjectLanguages::familyOf("c"), QString("cpp"));
        QCOMPARE(ProjectLanguages::familyOf("tsx"), QString("javascript"));
        QCOMPARE(ProjectLanguages::familyOf("python"), QString("python"));
        QVERIFY(ProjectLanguages::familyOf("rust").isEmpty());
        QCOMPARE(ProjectLanguages::title("cpp"), QString("C/C++"));
        QCOMPARE(ProjectLanguages::title("rust"), QString("rust"));
    }

    void projectSettingsApplyStaysOpenAndOkCloses()
    {
        const QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        QWidget window;
        window.resize(1200, 800);
        window.show();
        ProjectSettingsPopup popup(&toolchains, Theme::dark(), &window);
        const QSignalSpy applied(&popup, &ProjectSettingsPopup::applied);
        popup.popup(cppProject(dir.path(), toolchains));
        const auto *panel = popup.findChild<ProjectSetupPanel *>();

        panel->applyButton()->click();

        QCOMPARE(applied.size(), 1);
        QVERIFY(popup.isVisible());
        QVERIFY(ProjectStore::find(dir.path()).has_value());
        panel->okButton()->click();
        QCOMPARE(applied.size(), 2);
        QVERIFY(!popup.isVisible());
    }

    void projectSettingsCancelLeavesTheSetup()
    {
        const QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        QWidget window;
        window.show();
        ProjectSettingsPopup popup(&toolchains, Theme::dark(), &window);
        const QSignalSpy applied(&popup, &ProjectSettingsPopup::applied);
        popup.popup(cppProject(dir.path(), toolchains));

        popup.findChild<ProjectSetupPanel *>()->cancelButton()->click();

        QCOMPARE(applied.size(), 0);
        QVERIFY(!popup.isVisible());
        QVERIFY(!ProjectStore::find(dir.path()).has_value());
    }

    void listsTheProjects()
    {
        const QTemporaryDir dir;
        QDir(dir.path()).mkpath("alpha");
        QDir(dir.path()).mkpath("beta");
        ProjectConfig alpha = ProjectStore::touch(dir.filePath("alpha"));
        alpha.configured = true; // nothing to download
        ProjectStore::save(alpha);
        ProjectStore::touch(dir.filePath("beta"));
        ProjectStore::touch(dir.filePath("gone"));

        WelcomeWindow window;
        QCOMPARE(window.projectList()->count(), 3);
        QCOMPARE(window.projectList()->item(0)->text(), QString("gone")); // opened last

        QSignalSpy chosen(&window, &WelcomeWindow::projectChosen);
        window.open(dir.filePath("gone")); // missing: nothing
        QCOMPARE(chosen.size(), 0);
        window.open(dir.filePath("alpha"));
        QCOMPARE(chosen.size(), 1);
        QCOMPARE(chosen.first().first().value<ProjectConfig>().path, dir.filePath("alpha"));
    }

    void newProjectFormCreatesTheProjectWithARepository()
    {
        const QTemporaryDir dir;
        NewProjectForm form;
        form.nameEdit()->setText("demo");
        form.locationEdit()->setText(dir.path());
        form.recipeList()->setCurrentRow(1); // C++
        form.gitCheck()->setChecked(true);
        QSignalSpy created(&form, &NewProjectForm::created);

        form.createButton()->click();

        QCOMPARE(created.size(), 1);
        QCOMPARE(created.first().first().toString(), dir.filePath("demo"));
        QVERIFY(QFileInfo::exists(dir.filePath("demo/CMakeLists.txt")));
        QVERIFY(QFileInfo(dir.filePath("demo/.git")).isDir());
        const std::optional<ProjectConfig> project = ProjectStore::find(dir.filePath("demo"));
        QVERIFY(project.has_value());
        QVERIFY(!project->configured); // its languages are chosen next
        QCOMPARE(project->recipes().size(), 1);
        QVERIFY(project->recipes().first().startsWith("cpp-cmake"));
        QVERIFY(project->language("cpp"));
        QVERIFY(project->needsRecipeSetup); // CMake configures at the first opening
        QVERIFY(ClaudeTrust::isTrusted(dir.filePath("demo")));
    }

    void trustingAFolderKeepsTheRestOfClaudesState()
    {
        const QTemporaryDir dir;
        write(claudeConfig_.filePath(".claude.json"),
              R"({"theme": "dark", "projects": {"/elsewhere": {"hasTrustDialogAccepted": true}}})");
        QString error;

        QVERIFY2(ClaudeTrust::trust(dir.path(), &error), qPrintable(error));

        QVERIFY(ClaudeTrust::isTrusted(dir.path()));
        QVERIFY(ClaudeTrust::isTrusted("/elsewhere"));
        QFile file(claudeConfig_.filePath(".claude.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("theme").toString(), QString("dark"));
    }

    void trustLeavesAStateFileThatIsNotJsonAsItWas()
    {
        const QTemporaryDir dir;
        write(claudeConfig_.filePath(".claude.json"), "{ nope");
        QString error;

        QVERIFY(!ClaudeTrust::trust(dir.path(), &error));

        QVERIFY(!error.isEmpty());
        QFile file(claudeConfig_.filePath(".claude.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("{ nope"));
    }

    void newProjectFormCannotCreateOverAnExistingFolder()
    {
        const QTemporaryDir dir;
        write(dir.filePath("demo/main.c"), "");
        NewProjectForm form;
        form.locationEdit()->setText(dir.path());

        form.nameEdit()->setText("demo");
        QVERIFY(!form.createButton()->isEnabled());
        form.nameEdit()->setText("demo2");
        QVERIFY(form.createButton()->isEnabled());
        form.nameEdit()->setText("a/b");
        QVERIFY(!form.createButton()->isEnabled());
    }

    void newProjectRecipesKeepApartWhenTheStyleSheetChanges()
    {
        QWidget host;
        host.resize(1200, 900);
        host.show();
        auto *popup = new NewProjectPopup(Theme::dark(), &host);
        ThemeStyle::apply(Theme::dark()); // pads the templates once they are laid out

        popup->popup();

        const QListWidget *list = popup->findChild<QListWidget *>();
        QTRY_COMPARE(list->visualItemRect(list->item(1)).top(), list->visualItemRect(list->item(0)).bottom() + 1);
    }

    void newProjectGoesOnToItsSetup()
    {
        const QTemporaryDir dir;
        WelcomeWindow window;
        window.showNewProject();
        NewProjectForm *form = window.newProjectForm();
        form->nameEdit()->setText("demo");
        form->locationEdit()->setText(dir.path());
        form->recipeList()->setCurrentRow(0); // empty
        form->gitCheck()->setChecked(false);

        form->createButton()->click();

        QVERIFY(QFileInfo(dir.filePath("demo")).isDir());
        QVERIFY(!QFileInfo::exists(dir.filePath("demo/.git")));
        QVERIFY(window.findChild<ProjectSetupPanel *>()->isVisibleTo(&window));
    }

    // Downloads for real: DUET_NETWORK_TESTS=1.
    void downloadsTheMavenOfItsRecipe()
    {
        if (qEnvironmentVariableIsEmpty("DUET_NETWORK_TESTS"))
            QSKIP("Set DUET_NETWORK_TESTS=1 to download from the Internet");
        const std::optional<Recipe> recipe = RecipeLibrary::find(RecipeLibrary::defaultSources(), "java-maven");
        QVERIFY(recipe);
        const QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        QList<ToolRequirement> maven = recipe->tools();
        maven.removeIf([](const ToolRequirement &tool) { return tool.tool != "maven"; });
        const QList<InstallStep> steps = InstallPlan::plan({}, toolchains, maven);
        QCOMPARE(titles(steps), QStringList{"maven 3.9.16"});
        Installer installer(&toolchains);
        QSignalSpy finished(&installer, &Installer::finished);
        QSignalSpy stepFinished(&installer, &Installer::stepFinished);

        installer.run(steps);

        QVERIFY(finished.wait(300000));
        QCOMPARE(stepFinished.first().at(1).toString(), QString());
        QVERIFY(QFileInfo::exists(toolchains.installFolder("maven", "3.9.16") + "/bin/mvn"));
        QVERIFY(InstallPlan::plan({}, toolchains, maven).isEmpty());
    }

    // Downloads for real: DUET_NETWORK_TESTS=1.
    void downloadsNodeAndPyright()
    {
        if (qEnvironmentVariableIsEmpty("DUET_NETWORK_TESTS"))
            QSKIP("Set DUET_NETWORK_TESTS=1 to download from the Internet");
        const QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        ProjectConfig project;
        project.languages.append({"python", true, "3.12", {}, "pyright", "1.1.414"});
        const QList<InstallStep> steps = InstallPlan::plan(project, toolchains, {});
        QCOMPARE(titles(steps), (QStringList{"Node.js 24", "pyright 1.1.414"}));
        Installer installer(&toolchains);
        QSignalSpy finished(&installer, &Installer::finished);
        QSignalSpy results(&installer, &Installer::stepFinished);
        installer.run(steps);
        QVERIFY(finished.wait(300000));
        for (const QList<QVariant> &result : std::as_const(results))
            QCOMPARE(result.at(1).toString(), QString());
        QVERIFY(toolchains.runtime("node", "24"));
        QVERIFY(QFileInfo::exists(toolchains.installedExecutable("pyright", "1.1.414")));
    }

private:
    QTemporaryDir claudeConfig_;
};

QTEST_MAIN(TestProjects)
#include "tst_projects.moc"
