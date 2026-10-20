// SPDX-License-Identifier: GPL-3.0-only

#include "agent/vendor/claude/ClaudeTrust.h"
#include "projects/ProjectConfig.h"
#include "projects/ProjectDetector.h"
#include "projects/ProjectStore.h"
#include "projects/ProjectTemplates.h"
#include "run/RunConfiguration.h"
#include "toolchains/InstallPlan.h"
#include "toolchains/Installer.h"
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

// A tool installed by Duet, as Installer leaves it.
void fakeInstall(const Toolchains &toolchains, const QString &tool, const QString &version, const QString &executable)
{
    const QString folder = toolchains.installFolder(tool, version);
    write(folder + QLatin1Char('/') + executable, "#!/bin/sh\n");
    QFile::setPermissions(folder + QLatin1Char('/') + executable,
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    write(folder + QStringLiteral("/.complete"), "");
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

    void plansWhatIsMissing()
    {
        const QTemporaryDir dir;
        const Toolchains toolchains(dir.path(), false);
        ProjectConfig config;
        config.languages.append({"java", true, "17", {}, "jdtls", "1.62.0"});
        // jdtls itself runs on Java 21.
        QCOMPARE(titles(InstallPlan::plan(config, toolchains)), (QStringList{"JDK 17", "JDK 21", "jdtls 1.62.0"}));

        fakeInstall(toolchains, "jdk", "21", "bin/java");
        fakeInstall(toolchains, "jdtls", "1.62.0", "bin/jdtls");
        QCOMPARE(titles(InstallPlan::plan(config, toolchains)), (QStringList{"JDK 17"}));
        config.languages[0].runtimeVersion = "21";
        QVERIFY(InstallPlan::plan(config, toolchains).isEmpty());

        // A server needing Node brings one; disabled languages need nothing.
        config.languages.append({"python", true, "3.12", {}, "pyright", "1.1.414"});
        config.languages.append({"cpp", false, "c++20", {}, "clangd", "23.1.0"});
        QCOMPARE(titles(InstallPlan::plan(config, toolchains)), (QStringList{"Node.js 24", "pyright 1.1.414"}));
        QVERIFY(InstallPlan::unavailable(config, toolchains).join(' ').contains("Python 3.12"));
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

    void templateIsDetectedWithItsRuntime_data()
    {
        QTest::addColumn<QString>("family");
        QTest::addColumn<QString>("runtimeVersion");
        for (const QString family : {"cpp", "java", "javascript"})
            QTest::newRow(qPrintable(family)) << family << ToolCatalog::defaultRuntimeVersion(family);
        QTest::newRow("python") << QString("python") << QString("3.12");
    }

    void templateIsDetectedWithItsRuntime()
    {
        QFETCH(QString, family);
        QFETCH(QString, runtimeVersion);
        const QTemporaryDir dir;
        QString error;

        QVERIFY2(ProjectTemplates::create(family, dir.filePath("My App"), &error), qPrintable(error));

        const QList<DetectedLanguage> found = ProjectDetector::detect(dir.filePath("My App"));
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().language, family);
        QCOMPARE(found.first().runtimeVersion, runtimeVersion);
    }

    void templateConfiguresBuildRunAndDebug_data()
    {
        QTest::addColumn<QString>("family");
        for (const QString &family : ProjectLanguages::families())
            QTest::newRow(qPrintable(family)) << family;
    }

    void templateConfiguresBuildRunAndDebug()
    {
        QFETCH(QString, family);
        const QTemporaryDir dir;
        QString error;
        QVERIFY2(ProjectTemplates::create(family, dir.filePath("My App"), &error), qPrintable(error));
        QFile file(RunConfigurations::filePath(dir.filePath("My App")));
        QVERIFY(file.open(QIODevice::ReadOnly));

        const QList<RunConfiguration> configurations = RunConfigurations::parse(file.readAll(), &error);

        QVERIFY2(error.isEmpty(), qPrintable(error));
        QList<RunConfiguration::Kind> kinds;
        for (const RunConfiguration &configuration : configurations)
            kinds.append(configuration.kind);
        const QList<RunConfiguration::Kind> expected{RunConfiguration::Kind::Build, RunConfiguration::Kind::Run,
                                                     RunConfiguration::Kind::Debug};
        QCOMPARE(kinds, expected);
        QVERIFY(!configurations.last().command.contains("{{"));
    }

    void namesTheTemplateFilesAfterTheFolder()
    {
        const QTemporaryDir dir;
        QString error;

        QVERIFY(ProjectTemplates::create("python", dir.filePath("My App"), &error));

        QVERIFY(QFileInfo::exists(dir.filePath("My App/src/my_app/__main__.py")));
        QFile pyproject(dir.filePath("My App/pyproject.toml"));
        QVERIFY(pyproject.open(QIODevice::ReadOnly));
        QVERIFY(pyproject.readAll().contains("name = \"my_app\""));
    }

    void makesAnIdentifierOfTheProjectName()
    {
        QCOMPARE(ProjectTemplates::identifierOf("My App!"), QString("my_app"));
        QCOMPARE(ProjectTemplates::identifierOf("--web-front--"), QString("web_front"));
        QCOMPARE(ProjectTemplates::identifierOf("2048"), QString("project_2048"));
        QCOMPARE(ProjectTemplates::identifierOf("Éclair"), QString("clair"));
        QCOMPARE(ProjectTemplates::identifierOf("???"), QString("project"));
    }

    void templateRefusesAFolderThatIsNotEmpty()
    {
        const QTemporaryDir dir;
        write(dir.filePath("taken/notes.txt"), "keep me");
        QString error;

        QVERIFY(!ProjectTemplates::create("cpp", dir.filePath("taken"), &error));

        QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(dir.filePath("taken/CMakeLists.txt")));
    }

    void newProjectFormCreatesTheProjectWithARepository()
    {
        const QTemporaryDir dir;
        NewProjectForm form;
        form.nameEdit()->setText("demo");
        form.locationEdit()->setText(dir.path());
        form.templateList()->setCurrentRow(1); // C++
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

    void newProjectTemplatesKeepApartWhenTheStyleSheetChanges()
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
        form->templateList()->setCurrentRow(0); // empty
        form->gitCheck()->setChecked(false);

        form->createButton()->click();

        QVERIFY(QFileInfo(dir.filePath("demo")).isDir());
        QVERIFY(!QFileInfo::exists(dir.filePath("demo/.git")));
        QVERIFY(window.findChild<ProjectSetupPanel *>()->isVisibleTo(&window));
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
        const QList<InstallStep> steps = InstallPlan::plan(project, toolchains);
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
