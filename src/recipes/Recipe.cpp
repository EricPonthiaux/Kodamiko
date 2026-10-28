// SPDX-License-Identifier: GPL-3.0-only

#include "Recipe.h"

#include "RecipeSchema.h"
#include "system/Host.h"
#include "toolchains/ToolCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QUrl>

#include <algorithm>

namespace {

QStringList stringsOf(const QJsonValue &value)
{
    QStringList strings;
    for (const QJsonValue &item : value.toArray()) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty())
            strings.append(text);
    }
    return strings;
}

QMap<QString, QString> variablesOf(const QJsonObject &object)
{
    QMap<QString, QString> variables;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        variables.insert(it.key(), it.value().toVariant().toString());
    return variables;
}

RecipeEnvironment environmentOf(const QJsonObject &object)
{
    RecipeEnvironment environment;
    environment.visualStudio = object.value(QStringLiteral("visualStudio")).toBool();
    environment.pathFolders = stringsOf(object.value(QStringLiteral("path")));
    environment.variables = variablesOf(object.value(QStringLiteral("variables")).toObject());
    return environment;
}

// The requirements for this system: an entry with "systems" is for those
// only (Qt's kit is "gcc_64" on Linux, "macos" on macOS).
// The download of `downloads` for this system and processor, nullopt when
// it has none for them.
std::optional<QJsonObject> platformDownload(const QJsonObject &downloads)
{
    const QString system = Host::systemName();
    const QString processor = system + QLatin1Char('-') + ToolCatalog::currentPlatform().arch;
    for (const QString &key : {processor, system, QStringLiteral("any")}) {
        if (downloads.contains(key))
            return downloads.value(key).toObject();
    }
    return std::nullopt;
}

std::optional<ArchiveFormat> formatOf(const QString &name)
{
    if (name.endsWith(QLatin1String("zip")))
        return ArchiveFormat::Zip;
    if (name.endsWith(QLatin1String("tar.gz")) || name.endsWith(QLatin1String("tgz")))
        return ArchiveFormat::TarGz;
    if (name.endsWith(QLatin1String("7z")))
        return ArchiveFormat::SevenZip;
    return std::nullopt;
}

QString textOf(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toString().trimmed();
}

// Packages npm installs, and checks: no archive, no checksum.
DownloadSource npmSourceOf(const QJsonObject &entry, const QString &version)
{
    DownloadSource source;
    source.format = ArchiveFormat::Npm;
    for (QString package : stringsOf(entry.value(QStringLiteral("npm"))))
        source.npmPackages.append(package.replace(QLatin1String(Recipes::versionPlaceholder), version));
    if (source.npmPackages.isEmpty())
        source.error = QObject::tr("its download has no npm package");
    return source;
}

DownloadSource sourceOf(const QJsonObject &entry, const QString &version)
{
    if (entry.contains(QStringLiteral("npm")))
        return npmSourceOf(entry, version);
    DownloadSource source;
    const QString url = textOf(entry, "url").replace(QLatin1String(Recipes::versionPlaceholder), version);
    source.url = QUrl(url);
    source.fileName = source.url.fileName();
    source.checksum = textOf(entry, "checksum").toLower();
    const QString checksumUrl = textOf(entry, "checksumUrl")
                                    .replace(QLatin1String(Recipes::urlPlaceholder), url)
                                    .replace(QLatin1String(Recipes::versionPlaceholder), version);
    source.checksumUrl = QUrl(checksumUrl);
    if (textOf(entry, "algorithm") == QLatin1String("sha512"))
        source.algorithm = ChecksumAlgorithm::Sha512;
    const QString format = textOf(entry, "format");
    const std::optional<ArchiveFormat> archiveFormat = formatOf(format.isEmpty() ? source.fileName : format);
    if (archiveFormat)
        source.format = *archiveFormat;
    // A download Kodamiko cannot check is not installed.
    if (!source.url.isValid() || source.fileName.isEmpty())
        source.error = QObject::tr("its download has no URL");
    else if (source.checksum.isEmpty() && checksumUrl.isEmpty())
        source.error = QObject::tr("%1 has no checksum").arg(source.fileName);
    else if (!archiveFormat)
        source.error = QObject::tr("the format of %1 is unknown: give a \"format\"").arg(source.fileName);
    return source;
}

RecipeRequirement requirementOf(const QJsonObject &object)
{
    const QString version = textOf(object, "version");
    const std::optional<QJsonObject> download = platformDownload(object.value(QStringLiteral("download")).toObject());
    return {.tool = {.tool = textOf(object, "tool"),
                     .version = version,
                     .kit = textOf(object, "kit"),
                     .modules = stringsOf(object.value(QStringLiteral("modules"))),
                     .program = textOf(object, "program"),
                     .download = download ? std::optional(sourceOf(*download, version)) : std::nullopt,
                     .check = download ? textOf(*download, "check") : QString()},
            .pathFolders = stringsOf(object.value(QStringLiteral("path"))),
            .variables = variablesOf(object.value(QStringLiteral("variables")).toObject())};
}

QList<RecipeRequirement> requirementsOf(const QJsonValue &value)
{
    QList<RecipeRequirement> requirements;
    for (const QJsonValue &item : value.toArray()) {
        const QJsonObject object = item.toObject();
        const QStringList systems = stringsOf(object.value(QStringLiteral("systems")));
        if (!systems.isEmpty() && !systems.contains(Host::systemName()))
            continue;
        const RecipeRequirement requirement = requirementOf(object);
        if (!requirement.tool.tool.isEmpty())
            requirements.append(requirement);
    }
    return requirements;
}

QList<RecipePrompt> promptsOf(const QJsonValue &value)
{
    QList<RecipePrompt> prompts;
    for (const QJsonValue &item : value.toArray()) {
        const QJsonObject object = item.toObject();
        RecipePrompt prompt{.title = object.value(QStringLiteral("title")).toString().trimmed(),
                            .text = object.value(QStringLiteral("text")).toString()};
        if (!prompt.title.isEmpty() && !prompt.text.trimmed().isEmpty())
            prompts.append(prompt);
    }
    return prompts;
}

RecipeParameterValue parameterValueOf(const QJsonValue &item)
{
    if (!item.isObject())
        return {.value = item.toVariant().toString().trimmed(), .variables = {}};
    const QJsonObject object = item.toObject();
    return {.value = textOf(object, "value"),
            .variables = variablesOf(object.value(QStringLiteral("variables")).toObject())};
}

QList<RecipeParameter> parametersOf(const QJsonValue &value)
{
    QList<RecipeParameter> parameters;
    for (const QJsonValue &item : value.toArray()) {
        const QJsonObject object = item.toObject();
        RecipeParameter parameter{.name = textOf(object, "name"), .title = textOf(object, "title"), .values = {}};
        for (const QJsonValue &choice : object.value(QStringLiteral("values")).toArray()) {
            if (const RecipeParameterValue entry = parameterValueOf(choice); !entry.value.isEmpty())
                parameter.values.append(entry);
        }
        if (parameter.name.isEmpty() || parameter.values.isEmpty())
            continue;
        if (parameter.title.isEmpty())
            parameter.title = parameter.name;
        parameters.append(parameter);
    }
    return parameters;
}

QList<RecipeLanguageServer> languageServersOf(const QJsonValue &value)
{
    QList<RecipeLanguageServer> servers;
    for (const QJsonValue &item : value.toArray()) {
        const QJsonObject object = item.toObject();
        const RecipeLanguageServer server{.name = textOf(object, "name"),
                                          .tool = textOf(object, "tool"),
                                          .script = textOf(object, "script"),
                                          .arguments = stringsOf(object.value(QStringLiteral("arguments"))),
                                          .languages = stringsOf(object.value(QStringLiteral("languages"))),
                                          .triggerCharacters =
                                              object.value(QStringLiteral("triggerCharacters")).toString()};
        const bool isComplete =
            !server.name.isEmpty() && !server.tool.isEmpty() && !server.script.isEmpty() && !server.languages.isEmpty();
        if (isComplete)
            servers.append(server);
    }
    return servers;
}

QMap<QString, RecipeServerOptions> serverOptionsOf(const QJsonObject &object)
{
    QMap<QString, RecipeServerOptions> options;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QJsonObject server = it.value().toObject();
        options.insert(it.key(), {.initialization = server.value(QStringLiteral("initialization")).toObject(),
                                  .settings = server.value(QStringLiteral("settings")).toObject()});
    }
    return options;
}

// `text` with each {{name}} of `values` replaced.
QString filled(QString text, const QMap<QString, QString> &values)
{
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        text.replace(QStringLiteral("{{%1}}").arg(it.key()), it.value());
    return text;
}

QStringList filled(QStringList texts, const QMap<QString, QString> &values)
{
    for (QString &text : texts)
        text = filled(text, values);
    return texts;
}

QMap<QString, QString> filled(QMap<QString, QString> map, const QMap<QString, QString> &values)
{
    for (QString &text : map)
        text = filled(text, values);
    return map;
}

QJsonValue filled(const QJsonValue &json, const QMap<QString, QString> &values);

QJsonObject filled(QJsonObject object, const QMap<QString, QString> &values)
{
    for (auto it = object.begin(); it != object.end(); ++it)
        it.value() = filled(it.value(), values);
    return object;
}

QJsonValue filled(const QJsonValue &json, const QMap<QString, QString> &values)
{
    if (json.isString())
        return filled(json.toString(), values);
    if (json.isObject())
        return filled(json.toObject(), values);
    if (!json.isArray())
        return json;
    QJsonArray array;
    for (const QJsonValue &item : json.toArray())
        array.append(filled(item, values));
    return array;
}

void fillDownload(DownloadSource &source, const QMap<QString, QString> &values)
{
    source.url = QUrl(filled(source.url.toString(), values));
    source.fileName = filled(source.fileName, values);
    source.checksumUrl = QUrl(filled(source.checksumUrl.toString(), values));
    source.npmPackages = filled(source.npmPackages, values);
}

void fillRequirement(RecipeRequirement &requirement, const QMap<QString, QString> &values)
{
    requirement.tool.version = filled(requirement.tool.version, values);
    requirement.tool.check = filled(requirement.tool.check, values);
    if (requirement.tool.download)
        fillDownload(*requirement.tool.download, values);
    requirement.pathFolders = filled(requirement.pathFolders, values);
    requirement.variables = filled(requirement.variables, values);
}

// The placeholders of `parameters` for the values `chosen` gives them.
QMap<QString, QString> placeholdersOf(const QList<RecipeParameter> &parameters, const QMap<QString, QString> &chosen)
{
    QMap<QString, QString> placeholders;
    for (const RecipeParameter &parameter : parameters) {
        const QString wanted = chosen.value(parameter.name);
        const auto found = std::ranges::find(parameter.values, wanted, &RecipeParameterValue::value);
        const RecipeParameterValue &value = found != parameter.values.end() ? *found : parameter.values.first();
        placeholders.insert(parameter.name, value.value);
        placeholders.insert(value.variables);
    }
    return placeholders;
}

// The "run" array, read as .kodamiko/run.json's "configurations".
QList<RunConfiguration> runConfigurationsOf(const QJsonValue &value)
{
    const QJsonObject file{{QStringLiteral("configurations"), value.toArray()}};
    return RunConfigurations::parse(QJsonDocument(file).toJson(QJsonDocument::Compact));
}

void addSchemaProblems(const QByteArray &recipeJson, const QString &fileName, QStringList &warnings)
{
    for (const RecipeSchema::Problem &problem : RecipeSchema::check(recipeJson))
        warnings.append(QStringLiteral("%1:%2: %3").arg(fileName).arg(problem.line).arg(problem.message));
}

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

QString Recipe::pluginFolder() const
{
    const QString skills = QDir(folder).filePath(QStringLiteral("skills"));
    return QFileInfo(skills).isDir() ? folder : QString();
}

bool Recipe::isFor(const QString &system) const
{
    return systems.isEmpty() || systems.contains(system);
}

QList<RunConfiguration> Recipe::runConfigurationsFor(const QString &identifier) const
{
    QList<RunConfiguration> filled = runConfigurations;
    for (RunConfiguration &configuration : filled) {
        configuration.command.replace(QLatin1String(Recipes::identifierPlaceholder), identifier);
        configuration.folder.replace(QLatin1String(Recipes::identifierPlaceholder), identifier);
    }
    return filled;
}

QList<ToolRequirement> Recipe::tools() const
{
    QList<ToolRequirement> tools;
    for (const RecipeRequirement &requirement : requirements)
        tools.append(requirement.tool);
    return tools;
}

std::optional<RunConfiguration> Recipe::setupConfigurationFor(const QString &identifier) const
{
    if (setupCommands.isEmpty())
        return std::nullopt;
    // `&&` stops at the first failure, in cmd.exe as in a POSIX shell.
    QString command = setupCommands.join(QStringLiteral(" && "));
    command.replace(QLatin1String(Recipes::identifierPlaceholder), identifier);
    return RunConfiguration{.name = QLatin1String(Recipes::setupName),
                            .kind = RunConfiguration::Kind::Build,
                            .command = command,
                            .folder = {},
                            .environment = {},
                            .detected = false};
}

Recipe Recipe::withValues(const QMap<QString, QString> &chosen) const
{
    Recipe recipe = *this;
    recipe.values = placeholdersOf(parameters, chosen);
    const QMap<QString, QString> &placeholders = recipe.values;
    if (placeholders.isEmpty())
        return recipe;
    for (RecipeRequirement &requirement : recipe.requirements)
        fillRequirement(requirement, placeholders);
    for (RunConfiguration &configuration : recipe.runConfigurations) {
        configuration.command = filled(configuration.command, placeholders);
        configuration.folder = filled(configuration.folder, placeholders);
        configuration.environment = filled(configuration.environment, placeholders);
    }
    recipe.setupCommands = filled(recipe.setupCommands, placeholders);
    for (RecipePrompt &prompt : recipe.prompts)
        prompt.text = filled(prompt.text, placeholders);
    recipe.instructions = filled(recipe.instructions, placeholders);
    for (RecipeLanguageServer &server : recipe.languageServers)
        server.arguments = filled(server.arguments, placeholders);
    for (RecipeServerOptions &options : recipe.serverOptions) {
        options.initialization = filled(options.initialization, placeholders);
        options.settings = filled(options.settings, placeholders);
    }
    return recipe;
}

namespace Recipes {

std::optional<Recipe> parse(const QByteArray &recipeJson, const QString &folder, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(recipeJson, &parseError);
    if (!document.isObject()) {
        *error = document.isNull() ? parseError.errorString() : QObject::tr("recipe.json is not a JSON object");
        return std::nullopt;
    }
    const QJsonObject object = document.object();
    Recipe recipe;
    recipe.folder = QDir(folder).absolutePath();
    recipe.id = QFileInfo(recipe.folder).fileName();
    recipe.title = object.value(QStringLiteral("title")).toString().trimmed();
    if (recipe.title.isEmpty()) {
        *error = QObject::tr("recipe.json has no \"title\"");
        return std::nullopt;
    }
    recipe.description = object.value(QStringLiteral("description")).toString().trimmed();
    recipe.systems = stringsOf(object.value(QStringLiteral("systems")));
    recipe.language = textOf(object, "language");
    recipe.environment = environmentOf(object.value(QStringLiteral("environment")).toObject());
    recipe.requirements = requirementsOf(object.value(QStringLiteral("requires")));
    recipe.runConfigurations = runConfigurationsOf(object.value(QStringLiteral("run")));
    recipe.setupCommands = stringsOf(object.value(QStringLiteral("setup")));
    recipe.prompts = promptsOf(object.value(QStringLiteral("prompts")));
    recipe.parameters = parametersOf(object.value(QStringLiteral("parameters")));
    recipe.languageServers = languageServersOf(object.value(QStringLiteral("languageServers")));
    recipe.serverOptions = serverOptionsOf(object.value(QStringLiteral("serverOptions")).toObject());
    return recipe;
}

std::optional<Recipe> load(const QString &folder, QString *error, QStringList *warnings)
{
    QFile file(QDir(folder).filePath(QStringLiteral("recipe.json")));
    const QString fileName = QDir::toNativeSeparators(file.fileName());
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("Cannot read %1: %2").arg(fileName, file.errorString());
        return std::nullopt;
    }
    const QByteArray recipeJson = file.readAll();
    std::optional<Recipe> recipe = parse(recipeJson, folder, error);
    if (!recipe) {
        *error = QStringLiteral("%1: %2").arg(fileName, *error);
        return std::nullopt;
    }
    // Only for a recipe that is read: the error already says why one is not.
    if (warnings)
        addSchemaProblems(recipeJson, fileName, *warnings);
    recipe->instructions = readText(QDir(folder).filePath(QStringLiteral("instructions.md"))).trimmed();
    return recipe;
}

} // namespace Recipes
