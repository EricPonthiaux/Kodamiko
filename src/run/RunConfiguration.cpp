#include "RunConfiguration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool exists(const QString &folder, const QString &name)
{
    return QFileInfo::exists(folder + QLatin1Char('/') + name);
}

// A wrapper script of the project (mvnw, gradlew) as the shell runs it.
QString wrapper(const QString &name)
{
#ifdef Q_OS_WIN
    return name;
#else
    return QStringLiteral("./") + name;
#endif
}

RunConfiguration make(const QString &name, RunConfiguration::Kind kind, const QString &command)
{
    RunConfiguration configuration;
    configuration.name = name;
    configuration.kind = kind;
    configuration.command = command;
    configuration.detected = true;
    return configuration;
}

// The non-hidden presets of `key` in CMakePresets.json and CMakeUserPresets.json.
QStringList cmakePresets(const QString &folder, const QString &key)
{
    QStringList names;
    for (const QString &file : {QStringLiteral("CMakePresets.json"), QStringLiteral("CMakeUserPresets.json")}) {
        const QJsonObject root = QJsonDocument::fromJson(readFile(folder + QLatin1Char('/') + file)).object();
        for (const QJsonValue &value : root.value(key).toArray()) {
            const QJsonObject preset = value.toObject();
            const QString name = preset.value(QStringLiteral("name")).toString();
            if (!name.isEmpty() && !preset.value(QStringLiteral("hidden")).toBool() && !names.contains(name))
                names.append(name);
        }
    }
    return names;
}

void detectCMake(const QString &folder, QList<RunConfiguration> &found)
{
    using Kind = RunConfiguration::Kind;
    if (exists(folder, QStringLiteral("CMakePresets.json")) ||
        exists(folder, QStringLiteral("CMakeUserPresets.json"))) {
        for (const QString &name : cmakePresets(folder, QStringLiteral("buildPresets")))
            found.append(make(QStringLiteral("CMake: build %1").arg(name), Kind::Build,
                              QStringLiteral("cmake --build --preset %1").arg(name)));
        for (const QString &name : cmakePresets(folder, QStringLiteral("configurePresets")))
            found.append(make(QStringLiteral("CMake: configure %1").arg(name), Kind::Build,
                              QStringLiteral("cmake --preset %1").arg(name)));
        for (const QString &name : cmakePresets(folder, QStringLiteral("testPresets")))
            found.append(
                make(QStringLiteral("CTest: %1").arg(name), Kind::Test, QStringLiteral("ctest --preset %1").arg(name)));
        return;
    }
    if (!exists(folder, QStringLiteral("CMakeLists.txt")))
        return;
    // Configured on the first build; later configurations are quick.
    found.append(make(QStringLiteral("CMake: build"), Kind::Build,
                      QStringLiteral("cmake -S . -B build && cmake --build build")));
    found.append(
        make(QStringLiteral("CTest"), Kind::Test, QStringLiteral("ctest --test-dir build --output-on-failure")));
}

RunConfiguration::Kind scriptKind(const QString &script)
{
    using Kind = RunConfiguration::Kind;
    const QString name = script.toLower();
    if (name.contains(QLatin1String("test")) || name.startsWith(QLatin1String("e2e")))
        return Kind::Test;
    for (const char *prefix : {"build", "compile", "tsc", "typecheck", "type-check", "lint", "check"}) {
        if (name.startsWith(QLatin1String(prefix)))
            return Kind::Build;
    }
    return Kind::Run;
}

void detectPackageJson(const QString &folder, QList<RunConfiguration> &found)
{
    if (!exists(folder, QStringLiteral("package.json")))
        return;
    QString manager = QStringLiteral("npm");
    if (exists(folder, QStringLiteral("pnpm-lock.yaml")))
        manager = QStringLiteral("pnpm");
    else if (exists(folder, QStringLiteral("yarn.lock")))
        manager = QStringLiteral("yarn");
    else if (exists(folder, QStringLiteral("bun.lockb")) || exists(folder, QStringLiteral("bun.lock")))
        manager = QStringLiteral("bun");
    const QJsonObject scripts = QJsonDocument::fromJson(readFile(folder + QStringLiteral("/package.json")))
                                    .object()
                                    .value(QStringLiteral("scripts"))
                                    .toObject();
    for (auto it = scripts.constBegin(); it != scripts.constEnd(); ++it) {
        // npm's own hooks run around the script they name.
        if (it.key().startsWith(QLatin1String("pre")) && scripts.contains(it.key().mid(3)))
            continue;
        if (it.key().startsWith(QLatin1String("post")) && scripts.contains(it.key().mid(4)))
            continue;
        found.append(make(QStringLiteral("%1: %2").arg(manager, it.key()), scriptKind(it.key()),
                          QStringLiteral("%1 run %2").arg(manager, it.key())));
    }
}

void detectJava(const QString &folder, QList<RunConfiguration> &found)
{
    using Kind = RunConfiguration::Kind;
    if (exists(folder, QStringLiteral("pom.xml"))) {
        const QString mvn =
            exists(folder, QStringLiteral("mvnw")) ? wrapper(QStringLiteral("mvnw")) : QStringLiteral("mvn");
        found.append(make(QStringLiteral("Maven: compile"), Kind::Build, mvn + QStringLiteral(" compile")));
        found.append(make(QStringLiteral("Maven: package"), Kind::Build, mvn + QStringLiteral(" package")));
        found.append(make(QStringLiteral("Maven: test"), Kind::Test, mvn + QStringLiteral(" test")));
    }
    if (exists(folder, QStringLiteral("build.gradle")) || exists(folder, QStringLiteral("build.gradle.kts")) ||
        exists(folder, QStringLiteral("settings.gradle")) || exists(folder, QStringLiteral("settings.gradle.kts"))) {
        const QString gradle =
            exists(folder, QStringLiteral("gradlew")) ? wrapper(QStringLiteral("gradlew")) : QStringLiteral("gradle");
        found.append(make(QStringLiteral("Gradle: build"), Kind::Build, gradle + QStringLiteral(" build")));
        found.append(make(QStringLiteral("Gradle: test"), Kind::Test, gradle + QStringLiteral(" test")));
        const QByteArray script =
            readFile(folder + QStringLiteral("/build.gradle")) + readFile(folder + QStringLiteral("/build.gradle.kts"));
        if (script.contains("application"))
            found.append(make(QStringLiteral("Gradle: run"), Kind::Run, gradle + QStringLiteral(" run")));
    }
}

// The first makefile make reads, with its test and run targets.
QString makefileOf(const QString &folder)
{
    for (const QString &name :
         {QStringLiteral("GNUmakefile"), QStringLiteral("makefile"), QStringLiteral("Makefile")}) {
        if (exists(folder, name))
            return name;
    }
    return {};
}

void detectMake(const QString &folder, QList<RunConfiguration> &found)
{
    using Kind = RunConfiguration::Kind;
    const QString name = makefileOf(folder);
    if (name.isEmpty())
        return;
    found.append(make(QStringLiteral("make"), Kind::Build, QStringLiteral("make")));
    static const QRegularExpression target(QStringLiteral("^([A-Za-z0-9_.-]+)\\s*:(?!=)"),
                                           QRegularExpression::MultilineOption);
    QSet<QString> targets;
    for (const QRegularExpressionMatch &match :
         target.globalMatch(QString::fromUtf8(readFile(folder + QLatin1Char('/') + name))))
        targets.insert(match.captured(1));
    for (const QString &test : {QStringLiteral("test"), QStringLiteral("check")}) {
        if (targets.contains(test))
            found.append(make(QStringLiteral("make %1").arg(test), Kind::Test, QStringLiteral("make %1").arg(test)));
    }
    if (targets.contains(QStringLiteral("run")))
        found.append(make(QStringLiteral("make run"), Kind::Run, QStringLiteral("make run")));
}

void detectOthers(const QString &folder, QList<RunConfiguration> &found)
{
    using Kind = RunConfiguration::Kind;
    if (exists(folder, QStringLiteral("Cargo.toml"))) {
        found.append(make(QStringLiteral("Cargo: build"), Kind::Build, QStringLiteral("cargo build")));
        found.append(make(QStringLiteral("Cargo: run"), Kind::Run, QStringLiteral("cargo run")));
        found.append(make(QStringLiteral("Cargo: test"), Kind::Test, QStringLiteral("cargo test")));
    }
    if (exists(folder, QStringLiteral("go.mod"))) {
        found.append(make(QStringLiteral("Go: build"), Kind::Build, QStringLiteral("go build ./...")));
        found.append(make(QStringLiteral("Go: test"), Kind::Test, QStringLiteral("go test ./...")));
    }
    if (exists(folder, QStringLiteral("pytest.ini")) || exists(folder, QStringLiteral("conftest.py")) ||
        readFile(folder + QStringLiteral("/pyproject.toml")).contains("[tool.pytest")) {
        found.append(make(QStringLiteral("pytest"), Kind::Test, QStringLiteral("python -m pytest")));
    }
    detectMake(folder, found);
}

} // namespace

namespace RunConfigurations {

QString kindName(RunConfiguration::Kind kind)
{
    switch (kind) {
    case RunConfiguration::Kind::Build:
        return QStringLiteral("build");
    case RunConfiguration::Kind::Test:
        return QStringLiteral("test");
    case RunConfiguration::Kind::Run:
        break;
    }
    return QStringLiteral("run");
}

RunConfiguration::Kind kindNamed(const QString &name)
{
    if (name == QLatin1String("build"))
        return RunConfiguration::Kind::Build;
    if (name == QLatin1String("test"))
        return RunConfiguration::Kind::Test;
    return RunConfiguration::Kind::Run;
}

QString filePath(const QString &projectFolder)
{
    return QDir(projectFolder).filePath(QStringLiteral(".duet/run.json"));
}

QList<RunConfiguration> parse(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (document.isNull()) {
        if (error)
            *error = parseError.errorString();
        return {};
    }
    QList<RunConfiguration> configurations;
    for (const QJsonValue &value : document.object().value(QStringLiteral("configurations")).toArray()) {
        const QJsonObject object = value.toObject();
        RunConfiguration configuration;
        configuration.name = object.value(QStringLiteral("name")).toString().trimmed();
        configuration.kind = kindNamed(object.value(QStringLiteral("kind")).toString());
        configuration.command = object.value(QStringLiteral("command")).toString().trimmed();
        configuration.folder = object.value(QStringLiteral("folder")).toString();
        const QJsonObject environment = object.value(QStringLiteral("env")).toObject();
        for (auto it = environment.constBegin(); it != environment.constEnd(); ++it)
            configuration.environment.insert(it.key(), it.value().toVariant().toString());
        if (!configuration.name.isEmpty() && !configuration.command.isEmpty())
            configurations.append(configuration);
    }
    return configurations;
}

QByteArray serialize(const QList<RunConfiguration> &configurations)
{
    QJsonArray array;
    for (const RunConfiguration &configuration : configurations) {
        QJsonObject object{{QStringLiteral("name"), configuration.name},
                           {QStringLiteral("kind"), kindName(configuration.kind)},
                           {QStringLiteral("command"), configuration.command}};
        if (!configuration.folder.isEmpty())
            object.insert(QStringLiteral("folder"), configuration.folder);
        if (!configuration.environment.isEmpty()) {
            QJsonObject environment;
            for (auto it = configuration.environment.cbegin(); it != configuration.environment.cend(); ++it)
                environment.insert(it.key(), it.value());
            object.insert(QStringLiteral("env"), environment);
        }
        array.append(object);
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("configurations"), array}}).toJson(QJsonDocument::Indented);
}

QList<RunConfiguration> detect(const QString &projectFolder)
{
    QList<RunConfiguration> found;
    detectCMake(projectFolder, found);
    detectJava(projectFolder, found);
    detectPackageJson(projectFolder, found);
    detectOthers(projectFolder, found);
    std::stable_sort(found.begin(), found.end(), [](const RunConfiguration &a, const RunConfiguration &b) {
        return (a.kind == RunConfiguration::Kind::Build) > (b.kind == RunConfiguration::Kind::Build);
    });
    return found;
}

QList<RunConfiguration> all(const QString &projectFolder, QString *error)
{
    QList<RunConfiguration> configurations;
    QFile file(filePath(projectFolder));
    if (file.open(QIODevice::ReadOnly)) {
        QString parseError;
        configurations = parse(file.readAll(), &parseError);
        if (!parseError.isEmpty() && error)
            *error = QStringLiteral("%1: %2").arg(QStringLiteral(".duet/run.json"), parseError);
    }
    QSet<QString> names;
    for (const RunConfiguration &configuration : configurations)
        names.insert(configuration.name);
    for (const RunConfiguration &configuration : detect(projectFolder)) {
        if (!names.contains(configuration.name))
            configurations.append(configuration);
    }
    return configurations;
}

} // namespace RunConfigurations
