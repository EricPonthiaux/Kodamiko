// SPDX-License-Identifier: GPL-3.0-only

#include "projects/ProjectConfig.h"
#include "projects/ProjectDetector.h"
#include "recipes/DeveloperEnvironment.h"
#include "recipes/ProjectCreation.h"
#include "recipes/Recipe.h"
#include "recipes/RecipeLibrary.h"
#include "recipes/RecipeSchema.h"
#include "recipes/RecipeSetup.h"
#include "run/RunConfiguration.h"
#include "system/Host.h"
#include "terminal/ProcessSpec.h"
#include "toolchains/ToolCatalog.h"
#include "toolchains/Toolchains.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Duet's recipe `id` (from $DUET_RECIPES_DIR, the source tree's).
Recipe builtIn(const QString &id)
{
    const std::optional<Recipe> recipe = RecipeLibrary::find(RecipeLibrary::defaultSources(), id);
    if (!recipe)
        qFatal("No built-in recipe %s", qPrintable(id));
    return *recipe;
}

// The built-in recipe that starts a project of `family` on this system.
QString recipeOfFamily(const QString &family)
{
    if (family == QLatin1String("cpp"))
        return Host::systemName() == QLatin1String("windows") ? "cpp-cmake-msvc" : "cpp-cmake";
    if (family == QLatin1String("java"))
        return "java-maven";
    if (family == QLatin1String("javascript"))
        return "typescript-node";
    return "python-pyproject";
}

QStringList ids(const QList<Recipe> &recipes)
{
    QStringList list;
    for (const Recipe &recipe : recipes)
        list.append(recipe.id);
    return list;
}

// A recipe.json with these requirements.
QByteArray recipeJson(const QJsonArray &requirements)
{
    return QJsonDocument(QJsonObject{{"title", "T"}, {"requires", requirements}}).toJson();
}

} // namespace

class TestRecipes : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // The user's recipes of the tests, not the user's own.
        QCoreApplication::setOrganizationName(QStringLiteral("DuetTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_recipes"));
    }

    void readsARecipeFolder()
    {
        const QTemporaryDir dir;
        const QString folder = dir.filePath("qt-app");
        write(folder + "/recipe.json", R"({
            "title": "Qt App", "description": "A window.", "systems": ["windows"],
            "environment": {"visualStudio": true, "path": ["C:/tools"],
                            "variables": {"QT_DEBUG_PLUGINS": "1"}},
            "requires": [{"tool": "node", "version": "24"}, {"version": "no tool: skipped"},
                         {"tool": "qt", "version": "6.10", "kit": "msvc2022_64", "modules": ["qtcharts"],
                          "path": ["{{home}}/bin"], "variables": {"CMAKE_PREFIX_PATH": "{{home}}"}}],
            "run": [{"name": "Run", "kind": "run", "command": "build/{{identifier}}"}],
            "prompts": [{"title": "Add a Dialog", "text": "Add a dialog: "}, {"title": "", "text": "skipped"}]})");
        write(folder + "/instructions.md", "Use Qt Widgets.\n");
        write(folder + "/skills/qt/SKILL.md", "---\nname: qt\n---\n");
        QString error;

        const std::optional<Recipe> recipe = Recipes::load(folder, &error);

        QVERIFY2(recipe, qPrintable(error));
        QCOMPARE(recipe->id, QString("qt-app"));
        QCOMPARE(recipe->title, QString("Qt App"));
        QCOMPARE(recipe->systems, QStringList{"windows"});
        QVERIFY(recipe->environment.visualStudio);
        QCOMPARE(recipe->environment.pathFolders, QStringList{"C:/tools"});
        QCOMPARE(recipe->environment.variables.value("QT_DEBUG_PLUGINS"), QString("1"));
        QCOMPARE(recipe->runConfigurationsFor("demo").first().command, QString("build/demo"));
        const QList<ToolRequirement> tools{
            {.tool = "node", .version = "24", .kit = {}, .modules = {}},
            {.tool = "qt", .version = "6.10", .kit = "msvc2022_64", .modules = {"qtcharts"}}};
        QCOMPARE(recipe->tools(), tools);
        QCOMPARE(recipe->requirements.last().pathFolders, QStringList{"{{home}}/bin"});
        QCOMPARE(recipe->requirements.last().variables.value("CMAKE_PREFIX_PATH"), QString("{{home}}"));
        QCOMPARE(recipe->prompts.size(), 1);
        QCOMPARE(recipe->instructions, QString("Use Qt Widgets."));
        QCOMPARE(recipe->pluginFolder(), QDir(folder).absolutePath());
    }

    void refusesARecipeWithoutATitle()
    {
        QString error;

        QVERIFY(!Recipes::parse(R"({"description": "no title"})", "/recipes/x", &error));

        QVERIFY(error.contains("title"));
    }

    void everyBuiltInRecipeFollowsTheSchema()
    {
        const QDir builtIn(RecipeLibrary::defaultSources().builtIn);
        const QStringList ids = builtIn.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QVERIFY(!ids.isEmpty());
        for (const QString &id : ids) {
            QStringList messages;
            for (const RecipeSchema::Problem &problem :
                 RecipeSchema::check(read(builtIn.filePath(id + "/recipe.json"))))
                messages.append(QStringLiteral("line %1: %2").arg(problem.line).arg(problem.message));

            QVERIFY2(messages.isEmpty(), qPrintable(id + ": " + messages.join(" | ")));
        }
    }

    void theSchemaReportsWhatParseWouldSkip()
    {
        const QByteArray json = R"({
            "title": "T",
            "langage": "cpp",
            "requires": [{"tool": "maven", "download": {"any": {"url": "example.org/m.zip"}}}],
            "run": [{"name": "Run", "kind": "launch", "command": "a"}]
        })";

        const QList<RecipeSchema::Problem> problems = RecipeSchema::check(json);

        QCOMPARE(problems.size(), 3);
        QCOMPARE(problems[0],
                 (RecipeSchema::Problem{.line = 3, .message = "Unknown key “langage”: did you mean “language”?"}));
        QCOMPARE(problems[1], (RecipeSchema::Problem{.line = 4, .message = "Missing “checksum”"}));
        QCOMPARE(problems[2].line, 5);
        QVERIFY(problems[2].message.contains("\"run\""));
    }

    void theSchemaReportsATextThatIsNotJson()
    {
        const QList<RecipeSchema::Problem> problems = RecipeSchema::check("{\n  \"title\": \"T\",\n}");

        QCOMPARE(problems.size(), 1);
        QCOMPARE(problems[0].line, 3);
    }

    void theLibraryReportsTheSchemaProblemsOfTheRecipesItReads()
    {
        const QTemporaryDir dir;
        write(dir.filePath("odd/recipe.json"), R"({"title": "Odd", "sytems": ["linux"]})");
        write(dir.filePath("broken/recipe.json"), R"({"description": "no title"})");
        QStringList problems;

        const QList<Recipe> recipes =
            RecipeLibrary::available({.builtIn = dir.path(), .user = {}, .project = {}}, Host::systemName(), &problems);

        QCOMPARE(ids(recipes), QStringList{"odd"});
        QCOMPARE(problems.size(), 2);
        QVERIFY(problems[0].contains("title")); // "broken", refused
        QVERIFY(problems[1].endsWith("recipe.json:1: Unknown key “sytems”: did you mean “systems”?"));
    }

    void setupRunsTheCommandsInTurnWithTheIdentifier()
    {
        QString error;
        const std::optional<Recipe> recipe =
            Recipes::parse(R"({"title": "Node", "setup": ["npm install", "", "node scripts/{{identifier}}.js"]})",
                           "/recipes/x", &error);
        QVERIFY2(recipe, qPrintable(error));

        const std::optional<RunConfiguration> setup = recipe->setupConfigurationFor("demo");

        QVERIFY(setup);
        QCOMPARE(setup->name, QString(Recipes::setupName));
        QCOMPARE(setup->command, QString("npm install && node scripts/demo.js"));
    }

    void aRecipeWithoutSetupCommandsHasNoSetup()
    {
        QString error;
        const std::optional<Recipe> recipe = Recipes::parse(R"({"title": "Empty"})", "/recipes/x", &error);

        QVERIFY(!recipe->setupConfigurationFor("demo"));
    }

    void readsTheLanguageTheRecipeIsAttachedTo()
    {
        QString error;

        const std::optional<Recipe> recipe =
            Recipes::parse(R"({"title": "Qt", "language": "cpp"})", "/recipes/x", &error);

        QCOMPARE(recipe->language, QString("cpp"));
    }

    void everyBuiltInRecipeButTheEmptyOneHasALanguage()
    {
        for (const Recipe &recipe : RecipeLibrary::available(RecipeLibrary::defaultSources())) {
            const bool isEmpty = recipe.id == QLatin1String(RecipeLibrary::emptyId);
            QVERIFY2(recipe.language.isEmpty() == isEmpty, qPrintable(recipe.id));
        }
    }

    void aRecipeWithoutSkillsHasNoPluginFolder() { QCOMPARE(builtIn("python-pyproject").pluginFolder(), QString()); }

    void listsTheRecipesOfTheSystemTheEmptyOneFirst()
    {
        const QTemporaryDir dir;
        write(dir.filePath("builtIn/empty/recipe.json"), R"({"title": "Empty Folder"})");
        write(dir.filePath("builtIn/b/recipe.json"), R"({"title": "B", "systems": ["linux"]})");
        write(dir.filePath("builtIn/a/recipe.json"), R"({"title": "A"})");
        write(dir.filePath("builtIn/w/recipe.json"), R"({"title": "W", "systems": ["windows"]})");
        write(dir.filePath("builtIn/broken/recipe.json"), "{");
        QStringList problems;

        const QList<Recipe> recipes = RecipeLibrary::available(
            {.builtIn = dir.filePath("builtIn"), .user = {}, .project = {}}, "linux", &problems);

        QCOMPARE(ids(recipes), (QStringList{"empty", "a", "b"}));
        QCOMPARE(problems.size(), 1);
    }

    void aProjectRecipeReplacesTheUsersWhichReplacesDuets()
    {
        const QTemporaryDir dir;
        write(dir.filePath("builtIn/qt/recipe.json"), R"({"title": "Built-in"})");
        write(dir.filePath("user/qt/recipe.json"), R"({"title": "User"})");
        write(dir.filePath("builtIn/cpp/recipe.json"), R"({"title": "C++"})");
        const RecipeSources sources{.builtIn = dir.filePath("builtIn"), .user = dir.filePath("user"), .project = {}};
        RecipeSources withProject = sources;
        withProject.project = dir.filePath("project/.duet/recipes");
        write(withProject.project + "/qt/recipe.json", R"({"title": "Project"})");

        QCOMPARE(RecipeLibrary::find(sources, "qt")->title, QString("User"));
        QCOMPARE(RecipeLibrary::find(withProject, "qt")->title, QString("Project"));
        QCOMPARE(RecipeLibrary::find(withProject, "cpp")->title, QString("C++"));
        QVERIFY(!RecipeLibrary::find(withProject, "missing"));
    }

    void recipeIsDetectedWithItsRuntime_data()
    {
        QTest::addColumn<QString>("family");
        QTest::addColumn<QString>("runtimeVersion");
        for (const QString family : {"cpp", "java", "javascript"})
            QTest::newRow(qPrintable(family)) << family << ToolCatalog::defaultRuntimeVersion(family);
        QTest::newRow("python") << QString("python") << QString("3.12");
    }

    void recipeIsDetectedWithItsRuntime()
    {
        QFETCH(QString, family);
        QFETCH(QString, runtimeVersion);
        const QTemporaryDir dir;
        QString error;

        QVERIFY2(ProjectCreation::create(builtIn(recipeOfFamily(family)), dir.filePath("My App"), &error),
                 qPrintable(error));

        const QList<DetectedLanguage> found = ProjectDetector::detect(dir.filePath("My App"));
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().language, family);
        QCOMPARE(found.first().runtimeVersion, runtimeVersion);
    }

    void recipeConfiguresBuildRunAndDebug_data()
    {
        QTest::addColumn<QString>("id");
        for (const Recipe &recipe : RecipeLibrary::available(RecipeLibrary::defaultSources())) {
            if (recipe.id != QLatin1String(RecipeLibrary::emptyId))
                QTest::newRow(qPrintable(recipe.id)) << recipe.id;
        }
    }

    void recipeConfiguresBuildRunAndDebug()
    {
        QFETCH(QString, id);
        const QTemporaryDir dir;
        QString error;
        QVERIFY2(ProjectCreation::create(builtIn(id), dir.filePath("My App"), &error), qPrintable(error));

        const QList<RunConfiguration> configurations =
            RunConfigurations::parse(read(RunConfigurations::filePath(dir.filePath("My App"))), &error);

        QVERIFY2(error.isEmpty(), qPrintable(error));
        // Each kind once, in that order: a recipe can offer several of one.
        QList<RunConfiguration::Kind> kinds;
        for (const RunConfiguration &configuration : configurations) {
            if (kinds.isEmpty() || kinds.last() != configuration.kind)
                kinds.append(configuration.kind);
        }
        const QList<RunConfiguration::Kind> expected{RunConfiguration::Kind::Build, RunConfiguration::Kind::Run,
                                                     RunConfiguration::Kind::Debug};
        QCOMPARE(kinds, expected);
        QVERIFY(!configurations.last().command.contains("{{"));
    }

    void cppRecipeConfiguresBuildsAndRunsDefaultAndDebug_data()
    {
        QTest::addColumn<QString>("id");
        for (const Recipe &recipe : RecipeLibrary::available(RecipeLibrary::defaultSources())) {
            if (recipe.language == QLatin1String("cpp"))
                QTest::newRow(qPrintable(recipe.id)) << recipe.id;
        }
    }

    void cppRecipeConfiguresBuildsAndRunsDefaultAndDebug()
    {
        QFETCH(QString, id);

        const QList<RunConfiguration> configurations = builtIn(id).runConfigurationsFor("demo");

        QStringList names;
        for (const RunConfiguration &configuration : configurations)
            names.append(configuration.name);
        const QStringList expected{"Configure default", "Configure debug", "Build default", "Build debug",
                                   "Run default",       "Run debug",       "Debug"};
        QCOMPARE(names, expected);
    }

    void namesTheRecipeFilesAfterTheFolder()
    {
        const QTemporaryDir dir;
        QString error;

        QVERIFY(ProjectCreation::create(builtIn("python-pyproject"), dir.filePath("My App"), &error));

        QVERIFY(QFileInfo::exists(dir.filePath("My App/src/my_app/__main__.py")));
        QVERIFY(read(dir.filePath("My App/pyproject.toml")).contains("name = \"my_app\""));
        QVERIFY(QFileInfo::exists(dir.filePath("My App/.gitignore")));
    }

    void keepsTheRunJsonOfTheRecipesFiles()
    {
        const QTemporaryDir dir;
        write(dir.filePath("own/recipe.json"), R"({"title": "Own", "run": [{"name": "B", "command": "make"}]})");
        write(dir.filePath("own/files/.duet/run.json"), R"({"configurations": []})");
        QString error;
        const std::optional<Recipe> recipe = Recipes::load(dir.filePath("own"), &error);
        QVERIFY(recipe);

        QVERIFY2(ProjectCreation::create(*recipe, dir.filePath("project"), &error), qPrintable(error));

        QCOMPARE(read(RunConfigurations::filePath(dir.filePath("project"))), QByteArray(R"({"configurations": []})"));
    }

    void makesAnIdentifierOfTheProjectName()
    {
        QCOMPARE(ProjectCreation::identifierOf("My App!"), QString("my_app"));
        QCOMPARE(ProjectCreation::identifierOf("--web-front--"), QString("web_front"));
        QCOMPARE(ProjectCreation::identifierOf("2048"), QString("project_2048"));
        QCOMPARE(ProjectCreation::identifierOf("Éclair"), QString("clair"));
        QCOMPARE(ProjectCreation::identifierOf("???"), QString("project"));
    }

    void creationRefusesAFolderThatIsNotEmpty()
    {
        const QTemporaryDir dir;
        write(dir.filePath("taken/notes.txt"), "keep me");
        QString error;

        QVERIFY(!ProjectCreation::create(builtIn(recipeOfFamily("cpp")), dir.filePath("taken"), &error));

        QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(dir.filePath("taken/CMakeLists.txt")));
    }

    void keepsTheVariablesVcvarsSets()
    {
        QProcessEnvironment before;
        before.insert("HOME", "C:\\Users\\me");
        before.insert("PATH", "C:\\Windows");
        const QByteArray setOutput = "=C:=C:\\work\r\nHOME=C:\\Users\\me\r\nINCLUDE=C:\\VS\\include\r\n"
                                     "PATH=C:\\VS\\bin;C:\\Windows\r\n";

        const QMap<QString, QString> changed = DeveloperEnvironment::changedVariables(setOutput, before);

        QCOMPARE(changed.keys(), (QStringList{"INCLUDE", "PATH"}));
        QCOMPARE(changed.value("PATH"), QString("C:\\VS\\bin;C:\\Windows"));
    }

    void additionsHaveTheRecipesFoldersAndVariables()
    {
        RecipeEnvironment environment;
        environment.pathFolders = {"/tools/bin"};
        environment.variables.insert("MODE", "debug");
        QStringList problems;

        const EnvironmentAdditions additions = RecipeSetup::environmentAdditions(environment, &problems);

        QVERIFY(problems.isEmpty());
        QCOMPARE(additions.pathFolders, QStringList{"/tools/bin"});
        QCOMPARE(additions.variables.value("MODE"), QString("debug"));
    }

    void keepsTheRequirementsOfThisSystem()
    {
        const QString other = Host::systemName() == "linux" ? "macos" : "linux";
        const QString json = QString(R"({"title": "Qt", "requires": [
            {"tool": "qt", "kit": "here", "systems": ["%1"]}, {"tool": "qt", "kit": "there", "systems": ["%2"]},
            {"tool": "cmake"}]})")
                                 .arg(Host::systemName(), other);
        QString error;

        const std::optional<Recipe> recipe = Recipes::parse(json.toUtf8(), "/recipes/x", &error);

        QVERIFY2(recipe, qPrintable(error));
        QCOMPARE(recipe->requirements.size(), 2);
        QCOMPARE(recipe->requirements.first().tool.kit, QString("here"));
        QCOMPARE(recipe->requirements.last().tool.tool, QString("cmake"));
    }

    void readsTheDownloadOfThisSystem()
    {
        // Built as objects: moc takes the "//" of URLs in a raw string for a comment.
        const QJsonObject forThisSystem{{"url", "https://example.org/maven-{{version}}-bin.zip"},
                                        {"checksumUrl", "{{url}}.sha512"},
                                        {"algorithm", "sha512"},
                                        {"check", "bin/mvn"}};
        const QJsonObject forAny{{"url", "https://example.org/not-for-this-system.zip"}, {"checksum", "AB"}};
        const QJsonObject maven{{"tool", "maven"},
                                {"version", "3.9.16"},
                                {"program", "mvn"},
                                {"download", QJsonObject{{Host::systemName(), forThisSystem}, {"any", forAny}}}};
        QString error;

        const std::optional<Recipe> recipe = Recipes::parse(recipeJson({maven}), "/recipes/x", &error);

        QVERIFY2(recipe, qPrintable(error));
        const ToolRequirement tool = recipe->requirements.first().tool;
        QCOMPARE(tool.program, QString("mvn"));
        QCOMPARE(tool.check, QString("bin/mvn"));
        QVERIFY(tool.download);
        QCOMPARE(tool.download->url, QUrl("https://example.org/maven-3.9.16-bin.zip"));
        QCOMPARE(tool.download->fileName, QString("maven-3.9.16-bin.zip"));
        QCOMPARE(tool.download->checksumUrl, QUrl("https://example.org/maven-3.9.16-bin.zip.sha512"));
        QCOMPARE(tool.download->algorithm, ChecksumAlgorithm::Sha512);
        QCOMPARE(tool.download->format, ArchiveFormat::Zip);
        QVERIFY(tool.download->error.isEmpty());
    }

    void aDownloadWithoutChecksumOrFormatIsRefused()
    {
        const auto requirement = [](const char *tool, const QJsonObject &download) {
            return QJsonObject{{"tool", tool}, {"version", "1"}, {"download", QJsonObject{{"any", download}}}};
        };
        const QJsonArray requirements{
            requirement("a", {{"url", "https://example.org/a.zip"}}),
            requirement("b", {{"url", "https://example.org/b.bin"}, {"checksum", "ab"}}),
            requirement("c", {{"url", "https://example.org/c"}, {"checksum", "ab"}, {"format", "tar.gz"}})};
        QString error;

        const std::optional<Recipe> recipe = Recipes::parse(recipeJson(requirements), "/recipes/x", &error);

        QVERIFY2(recipe, qPrintable(error));
        QVERIFY(recipe->requirements.at(0).tool.download->error.contains("checksum"));
        QVERIFY(recipe->requirements.at(1).tool.download->error.contains("format"));
        QVERIFY(recipe->requirements.at(2).tool.download->error.isEmpty());
        QCOMPARE(recipe->requirements.at(2).tool.download->format, ArchiveFormat::TarGz);
    }

    void aRequiredToolGoesWhereTheRecipeSays()
    {
        const QTemporaryDir dir;
        const Toolchains toolchains(dir.path(), false);
        const QString qt = toolchains.qtInstallFolder("6.10.3", "msvc2022_64");
        write(qt + "/lib/cmake/Qt6/Qt6Config.cmake", "");
        QDir().mkpath(qt + "/bin");
        const QList<RecipeRequirement> required{
            {.tool = {.tool = "qt", .version = "6.10", .kit = "msvc2022_64", .modules = {}},
             .pathFolders = {"{{home}}/bin", "{{home}}/no-such-folder"},
             .variables = {{"CMAKE_PREFIX_PATH", "{{home}}"}}}};

        const EnvironmentAdditions additions = RecipeSetup::requirementAdditions(required, toolchains);

        QCOMPARE(additions.variables.value("CMAKE_PREFIX_PATH"), QDir::toNativeSeparators(qt));
        QCOMPARE(additions.pathFolders, QStringList{qt + "/bin"});
    }

    void aMissingToolAddsNothing()
    {
        const QTemporaryDir dir;
        const Toolchains toolchains(dir.path(), false);
        const QList<RecipeRequirement> required{{.tool = {.tool = "jdk", .version = "21", .kit = {}, .modules = {}},
                                                 .pathFolders = {"{{home}}/bin"},
                                                 .variables = {{"JAVA_HOME", "{{home}}"}}}};

        const EnvironmentAdditions additions = RecipeSetup::requirementAdditions(required, toolchains);

        QVERIFY(additions.pathFolders.isEmpty());
        QVERIFY(additions.variables.isEmpty());
    }

    void additionsPutTheirFoldersFirstInThePath()
    {
        QProcessEnvironment environment;
        environment.insert("PATH", QDir::toNativeSeparators("/usr/bin"));
        const EnvironmentAdditions additions{.variables = {{"MODE", "debug"}}, .pathFolders = {"/a", "/b"}};

        addTo(environment, additions);

        const QString separator = QDir::listSeparator();
        QCOMPARE(environment.value("PATH"), QDir::toNativeSeparators("/a") + separator +
                                                QDir::toNativeSeparators("/b") + separator +
                                                QDir::toNativeSeparators("/usr/bin"));
        QCOMPARE(environment.value("MODE"), QString("debug"));
    }

    void terminalsGetTheAdditions()
    {
        setTerminalAdditions({.variables = {{"DUET_TEST_RECIPE", "on"}}, .pathFolders = {}});

        const QString value = terminalEnvironment().value("DUET_TEST_RECIPE");
        setTerminalAdditions({});

        QCOMPARE(value, QString("on"));
        QVERIFY(!terminalEnvironment().contains("DUET_TEST_RECIPE"));
    }
};

QTEST_MAIN(TestRecipes)
#include "tst_recipes.moc"
