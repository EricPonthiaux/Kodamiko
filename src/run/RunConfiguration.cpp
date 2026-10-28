// SPDX-License-Identifier: GPL-3.0-only

#include "RunConfiguration.h"

#include "system/Host.h"
#include "system/Shell.h"

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

// A wrapper script of the project (mvnw) as the shell runs it.
QString wrapper(const QString &name)
{
    return Shell::programInWorkingFolder(name);
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
    case RunConfiguration::Kind::Debug:
        return QStringLiteral("debug");
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
    if (name == QLatin1String("debug"))
        return RunConfiguration::Kind::Debug;
    if (name == QLatin1String("test"))
        return RunConfiguration::Kind::Test;
    return RunConfiguration::Kind::Run;
}

QString filePath(const QString &projectFolder)
{
    return QDir(projectFolder).filePath(QStringLiteral(".kodamiko/run.json"));
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
        for (const QJsonValue &system : object.value(QStringLiteral("systems")).toArray())
            configuration.systems.append(system.toString());
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
        if (!configuration.systems.isEmpty())
            object.insert(QStringLiteral("systems"), QJsonArray::fromStringList(configuration.systems));
        array.append(object);
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("configurations"), array}}).toJson(QJsonDocument::Indented);
}

bool isForThisSystem(const RunConfiguration &configuration)
{
    return configuration.systems.isEmpty() || configuration.systems.contains(Host::systemName());
}

QList<RunConfiguration> detect(const QString &projectFolder)
{
    QList<RunConfiguration> found;
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
            *error = QStringLiteral("%1: %2").arg(QStringLiteral(".kodamiko/run.json"), parseError);
    }
    configurations.removeIf([](const RunConfiguration &configuration) { return !isForThisSystem(configuration); });
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
