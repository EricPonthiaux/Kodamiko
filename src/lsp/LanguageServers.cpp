// SPDX-License-Identifier: GPL-3.0-only

#include "LanguageServers.h"

#include "LspClient.h"
#include "system/Host.h"
#include "toolchains/JavaRuntimes.h"
#include "toolchains/ToolCatalog.h"
#include "toolchains/Toolchains.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace {

// As CLion: a symbol of a header not included yet comes with its #include
// (header insertion), and symbols of every namespace are offered, qualified
// (all scopes). Parsing always: otherwise, while the file's headers are being
// read, clangd answers with mere words (the local providers already give
// those, at once).
QStringList clangdArguments()
{
    QStringList arguments = {QStringLiteral("--background-index"), QStringLiteral("--header-insertion=iwyu"),
                             QStringLiteral("--all-scopes-completion"), QStringLiteral("--pch-storage=memory"),
                             QStringLiteral("--completion-parse=always")};
    // On Windows, LLVM runs the default "low" priority in
    // THREAD_MODE_BACKGROUND_BEGIN, which also drops the threads' I/O and
    // memory priority: indexing a project's thousands of headers crawls.
    if (Host::operatingSystem() == OperatingSystem::Windows)
        arguments << QStringLiteral("--background-index-priority=normal");
    return arguments;
}

// The folder of jdtls' installation configured for `system`.
QString jdtlsConfiguration(OperatingSystem system)
{
    switch (system) {
    case OperatingSystem::Windows:
        return QStringLiteral("config_win");
    case OperatingSystem::MacOS:
        return QStringLiteral("config_mac");
    case OperatingSystem::Linux:
        return QStringLiteral("config_linux");
    }
    return {};
}

} // namespace

LanguageServers::LanguageServers(QObject *parent)
    : QObject(parent)
{
}

LanguageServers::~LanguageServers()
{
    stopAll();
}

const QList<LanguageServerSpec> &LanguageServers::known()
{
    static const QList<LanguageServerSpec> specs = {
        {QStringLiteral("clangd"),
         QStringLiteral("clangd"),
         clangdArguments(),
         {QStringLiteral("c"), QStringLiteral("cpp")},
         QStringLiteral(".>:")},
        // Its workspace data goes to a cache folder (see clientFor). "@":
        // the annotations.
        {QStringLiteral("jdtls"), QStringLiteral("jdtls"), {}, {QStringLiteral("java")}, QStringLiteral(".@")},
        // "@": the decorators.
        {QStringLiteral("typescript-language-server"),
         QStringLiteral("typescript-language-server"),
         {QStringLiteral("--stdio")},
         {QStringLiteral("javascript"), QStringLiteral("typescript"), QStringLiteral("tsx")},
         QStringLiteral(".@")},
        {QStringLiteral("pyright"),
         QStringLiteral("pyright-langserver"),
         {QStringLiteral("--stdio")},
         {QStringLiteral("python")},
         QStringLiteral(".")},
        {QStringLiteral("pylsp"), QStringLiteral("pylsp"), {}, {QStringLiteral("python")}, QStringLiteral(".")},
    };
    return specs;
}

QString LanguageServers::protocolLanguage(const QString &languageId)
{
    if (languageId == QLatin1String("tsx"))
        return QStringLiteral("typescriptreact");
    return languageId;
}

QString LanguageServers::programPath(const QString &program) const
{
    auto it = programPaths_.find(program);
    if (it == programPaths_.end())
        it = programPaths_.insert(program, QStandardPaths::findExecutable(program));
    return it.value();
}

const LanguageSetup *LanguageServers::setupFor(const QString &languageId) const
{
    const QString family = ProjectLanguages::familyOf(languageId);
    return family.isEmpty() ? nullptr : project_.language(family);
}

QString LanguageServers::programFor(const LanguageServerSpec &spec) const
{
    for (const LanguageSetup &setup : project_.languages) {
        if (setup.server != spec.name || !toolchains_ ||
            setup.serverVersion == QLatin1String(ProjectLanguages::systemVersion))
            continue;
        const QString downloaded = toolchains_->installedExecutable(spec.name, setup.serverVersion);
        if (!downloaded.isEmpty())
            return downloaded;
    }
    return programPath(spec.program);
}

std::optional<LanguageServerSpec> LanguageServers::specFor(const QString &languageId) const
{
    auto cached = specs_.constFind(languageId);
    if (cached == specs_.constEnd())
        cached = specs_.insert(languageId, findSpec(languageId));
    return cached.value();
}

std::optional<LanguageServerSpec> LanguageServers::findSpec(const QString &languageId) const
{
    if (const LanguageSetup *setup = setupFor(languageId)) {
        // The project chose: that server or none.
        if (!setup->enabled || setup->server.isEmpty())
            return std::nullopt;
        for (const LanguageServerSpec &spec : known()) {
            if (spec.name == setup->server && spec.languages.contains(languageId) && !programFor(spec).isEmpty())
                return spec;
        }
        return std::nullopt;
    }
    for (const LanguageServerSpec &spec : known()) {
        if (spec.languages.contains(languageId) && !programPath(spec.program).isEmpty())
            return spec;
    }
    return std::nullopt;
}

QStringList LanguageServers::installed() const
{
    QStringList names;
    for (const LanguageServerSpec &spec : known()) {
        if (!programPath(spec.program).isEmpty() && !names.contains(spec.name))
            names.append(spec.name);
    }
    if (toolchains_) {
        for (const LanguageServerSpec &spec : known()) {
            for (const QString &version : toolchains_->installedVersions(spec.name))
                names.append(QStringLiteral("%1 %2").arg(spec.name, version));
        }
    }
    return names;
}

void LanguageServers::setFolder(const QString &folder)
{
    if (folder == folder_ && project_.languages.isEmpty())
        return;
    stopAll();
    folder_ = folder;
    project_ = {};
    specs_.clear();
}

void LanguageServers::setProject(const ProjectConfig &project, Toolchains *toolchains)
{
    // Downloads made meanwhile are seen (Duet's installs are listed from
    // disk each time). The machine's runtimes are not probed again: that
    // runs programs, and the installer refreshes after a download.
    programPaths_.clear();
    specs_.clear();
    toolchains_ = toolchains;
    const QString folder = QDir(project.path).absolutePath();
    if (folder == folder_ && project.languages == project_.languages)
        return;
    stopAll();
    folder_ = folder;
    project_ = project;
}

void LanguageServers::setEnabled(bool enabled)
{
    enabled_ = enabled;
    if (!enabled)
        stopAll();
}

void LanguageServers::stopAll()
{
    qDeleteAll(clients_); // each one asks its server to stop
    clients_.clear();
    documents_.clear();
    warmed_.clear();
    failures_.clear(); // a new folder or a new setting: worth trying again
    if (!std::exchange(tasks_, {}).isEmpty())
        emit tasksChanged();
    const QStringList reported = diagnostics_.keys();
    diagnostics_.clear();
    for (const QString &path : reported)
        emit diagnosticsChanged(path);
}

QString LanguageServers::failure(const QString &languageId) const
{
    const std::optional<LanguageServerSpec> spec = specFor(languageId);
    return spec ? failures_.value(spec->name) : QString();
}

void LanguageServers::retry(const QString &languageId)
{
    const std::optional<LanguageServerSpec> spec = specFor(languageId);
    if (!spec)
        return;
    failures_.remove(spec->name);
    delete clients_.take(spec->name);
    for (auto it = documents_.begin(); it != documents_.end();) {
        if (it.value().server == spec->name)
            it = documents_.erase(it);
        else
            ++it;
    }
    emit serverFailed(spec->name, spec->languages, QString());
}

QStringList LanguageServers::describe() const
{
    QStringList lines;
    for (const LanguageServerSpec &spec : known()) {
        const QString path = programPath(spec.program);
        if (path.isEmpty())
            continue;
        QString state;
        if (failures_.contains(spec.name))
            state = failures_.value(spec.name);
        else if (const LspClient *client = clients_.value(spec.name))
            state = client->isReady() ? tr("running") : tr("starting");
        else
            state = tr("starts with the first file of its language");
        lines.append(QStringLiteral("%1 — %2").arg(spec.name, state));
    }
    return lines;
}

std::optional<LanguageServers::Launch> LanguageServers::jdtlsLaunch(const QString &jdtlsProgram,
                                                                    const QString &dataFolder,
                                                                    const std::optional<JavaRuntime> &java,
                                                                    QString *error)
{
    if (!java || java->majorVersion < 21) {
        const std::optional<JavaRuntime> found = JavaRuntimes::newestOfAll();
        *error = found ? tr("jdtls needs Java 21 or newer; the newest Java found is Java %1 (%2). Install a recent "
                            "JDK (or point JAVA_HOME to one), then Retry.")
                             .arg(found->majorVersion)
                             .arg(found->home)
                       : tr("jdtls needs Java 21 or newer, and no Java was found. Install a recent JDK, then "
                            "Retry.");
        return std::nullopt;
    }

    // The installation: bin/jdtls next to plugins/ and config_<system>/.
    const QDir base(QFileInfo(QFileInfo(jdtlsProgram).canonicalFilePath()).absolutePath() + QStringLiteral("/.."));
    const QStringList launchers = QDir(base.filePath(QStringLiteral("plugins")))
                                      .entryList({QStringLiteral("org.eclipse.equinox.launcher_*.jar"),
                                                  QStringLiteral("org.eclipse.equinox.launcher.jar")},
                                                 QDir::Files);
    QString config = jdtlsConfiguration(Host::operatingSystem());
    if (QSysInfo::currentCpuArchitecture().startsWith(QLatin1String("arm")) &&
        base.exists(config + QStringLiteral("_arm")))
        config += QStringLiteral("_arm");

    Launch launch;
    launch.javaHome = java->home;
    if (launchers.isEmpty() || !base.exists(config)) {
        // An unusual layout: its own script, told which Java to use.
        launch.program = jdtlsProgram;
        launch.arguments = {QStringLiteral("--java-executable"), java->executable, QStringLiteral("-data"), dataFolder};
        return launch;
    }
    // As the jdtls script does.
    launch.program = java->executable;
    if (java->majorVersion >= 24)
        launch.arguments << QStringLiteral("-Djdk.xml.maxGeneralEntitySizeLimit=0")
                         << QStringLiteral("-Djdk.xml.totalEntitySizeLimit=0");
    launch.arguments << QStringLiteral("-Declipse.application=org.eclipse.jdt.ls.core.id1")
                     << QStringLiteral("-Dosgi.bundles.defaultStartLevel=4")
                     << QStringLiteral("-Declipse.product=org.eclipse.jdt.ls.core.product")
                     << QStringLiteral("-Dosgi.checkConfiguration=true")
                     << QStringLiteral("-Dosgi.sharedConfiguration.area=") + base.filePath(config)
                     << QStringLiteral("-Dosgi.sharedConfiguration.area.readOnly=true")
                     << QStringLiteral("-Dosgi.configuration.cascaded=true") << QStringLiteral("-Xms1G")
                     << QStringLiteral("--add-modules=ALL-SYSTEM") << QStringLiteral("--add-opens")
                     << QStringLiteral("java.base/java.util=ALL-UNNAMED") << QStringLiteral("--add-opens")
                     << QStringLiteral("java.base/java.lang=ALL-UNNAMED") << QStringLiteral("-jar")
                     << base.filePath(QStringLiteral("plugins/") + launchers.first()) << QStringLiteral("-data")
                     << dataFolder;
    return launch;
}

LspClient *LanguageServers::runningClientFor(const QString &languageId) const
{
    const std::optional<LanguageServerSpec> spec = specFor(languageId);
    if (!enabled_ || !spec)
        return nullptr;
    LspClient *client = clients_.value(spec->name);
    return client && !client->hasFailed() ? client : nullptr;
}

// How a server is started: its command, and the environment it runs in.
struct LanguageServers::ServerCommand {
    QString program;
    QStringList arguments;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

    void prependPath(const QString &folder)
    {
        environment.insert(QStringLiteral("PATH"),
                           folder + QDir::listSeparator() + environment.value(QStringLiteral("PATH")));
    }
};

LspClient *LanguageServers::clientFor(const QString &languageId)
{
    if (!enabled_ || folder_.isEmpty())
        return nullptr;
    const std::optional<LanguageServerSpec> spec = specFor(languageId);
    if (!spec)
        return nullptr;
    if (failures_.contains(spec->name))
        return nullptr; // tried already: said why
    if (LspClient *client = clients_.value(spec->name))
        return client->hasFailed() ? nullptr : client;

    ServerCommand command{programFor(*spec), spec->arguments};
    // The project's runtime: its JDK, its Python.
    const LanguageSetup *setup = setupFor(languageId);
    std::optional<RuntimeInstall> runtime;
    if (setup && toolchains_)
        runtime = toolchains_->runtimeFor(*setup);
    const RuntimeInstall *projectRuntime = runtime ? &*runtime : nullptr;

    if (!runScriptWithNode(*spec, command))
        return nullptr;

    const bool isJdtls = spec->name == QLatin1String("jdtls");
    if (isJdtls && !launchJdtls(*spec, projectRuntime, command))
        return nullptr;
    return startClient(*spec, command, setup, projectRuntime);
}

void LanguageServers::fail(const LanguageServerSpec &spec, const QString &error)
{
    failures_.insert(spec.name, error);
    emit serverFailed(spec.name, spec.languages, error);
}

bool LanguageServers::runScriptWithNode(const LanguageServerSpec &spec, ServerCommand &command)
{
    // Duet's npm installs are scripts, run with a Node recent enough.
    const ToolSpec *tool = ToolCatalog::find(spec.name);
    const bool isDuetScript =
        tool && tool->script && toolchains_ && command.program.startsWith(toolchains_->root() + QLatin1Char('/'));
    if (!isDuetScript)
        return true;
    const std::optional<RuntimeInstall> node =
        toolchains_->runtimeAtLeast(QStringLiteral("node"), tool->minimumRuntime);
    if (!node) {
        fail(spec, tr("%1 needs Node.js %2 or newer: open the project settings to download it.")
                       .arg(spec.name)
                       .arg(tool->minimumRuntime));
        return false;
    }
    command.arguments.prepend(command.program);
    command.program = node->executable;
    command.prependPath(QFileInfo(node->executable).absolutePath());
    return true;
}

std::optional<JavaRuntime> LanguageServers::jdtlsJava() const
{
    // jdtls itself runs on Java 21 or more, whatever the project's.
    if (!toolchains_)
        return JavaRuntimes::newest(21);
    if (const std::optional<RuntimeInstall> jdk = toolchains_->runtimeAtLeast(QStringLiteral("jdk"), 21))
        return JavaRuntime{jdk->home, jdk->executable, jdk->version.toInt()};
    return std::nullopt;
}

bool LanguageServers::launchJdtls(const LanguageServerSpec &spec, const RuntimeInstall *runtime, ServerCommand &command)
{
    // jdtls keeps its index in a folder of its own, one per project.
    const QString id =
        QString::fromLatin1(QCryptographicHash::hash(folder_.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    const QString data =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/jdtls/") + id;
    QDir().mkpath(data);
    QString error;
    const std::optional<Launch> launch = jdtlsLaunch(command.program, data, jdtlsJava(), &error);
    if (!launch) {
        fail(spec, error);
        return false;
    }
    command.program = launch->program;
    command.arguments = launch->arguments;
    // The project's build (Maven) runs with the project's JDK.
    const QString javaHome = runtime ? runtime->home : launch->javaHome;
    command.environment.insert(QStringLiteral("JAVA_HOME"), javaHome);
    command.prependPath(javaHome + QStringLiteral("/bin"));
    return true;
}

LspClient *LanguageServers::startClient(const LanguageServerSpec &spec,
                                        const ServerCommand &command,
                                        const LanguageSetup *setup,
                                        const RuntimeInstall *runtime)
{
    const QString name = spec.name;
    auto *client = new LspClient(command.program, command.arguments, folder_, this);
    client->setEnvironment(command.environment);
    // Without a project setup too: how completion behaves.
    const Options options = optionsFor(name, setup ? *setup : LanguageSetup(), runtime ? runtime->home : QString(),
                                       runtime ? runtime->executable : QString());
    if (!options.initialization.isEmpty())
        client->setInitializationOptions(options.initialization);
    if (!options.settings.isEmpty())
        client->setSettings(options.settings);
    for (auto handler = requestHandlers_.cbegin(); handler != requestHandlers_.cend(); ++handler)
        client->setRequestHandler(handler.key(), handler.value());
    clients_.insert(name, client);
    connect(client, &LspClient::ready, this, [this, name] { emit statusMessage(tr("%1 is ready").arg(name)); });
    connect(
        client, &LspClient::notificationReceived, this,
        [this, name](const QString &method, const QJsonValue &params) { receiveNotification(name, method, params); });
    connect(client, &LspClient::failed, this, [this, spec](const QString &message) {
        // Its documents must be opened again if it is restarted.
        forgetDocuments(spec.name);
        forgetTasks(spec.name);
        fail(spec, message);
    });
    client->start();
    emit statusMessage(tr("Starting %1…").arg(name));
    return client;
}

void LanguageServers::receiveNotification(const QString &server, const QString &method, const QJsonValue &params)
{
    if (method == QLatin1String("$/progress")) {
        if (const std::optional<LspProtocol::Progress> progress = LspProtocol::parseProgress(params))
            updateTask(server, *progress);
        return;
    }
    if (method != QLatin1String("textDocument/publishDiagnostics"))
        return;
    const QString path = LspProtocol::pathOf(params.toObject().value("uri").toString());
    if (path.isEmpty())
        return;
    diagnostics_.insert(path, params.toObject().value("diagnostics").toArray());
    emit diagnosticsChanged(path);
}

QList<LanguageServers::Task> LanguageServers::tasks() const
{
    QList<Task> tasks;
    tasks.reserve(tasks_.size());
    for (const TrackedTask &tracked : tasks_)
        tasks.append(tracked.task);
    return tasks;
}

void LanguageServers::updateTask(const QString &server, const LspProtocol::Progress &progress)
{
    using Kind = LspProtocol::Progress::Kind;
    const auto tracked = std::ranges::find_if(
        tasks_, [&](const TrackedTask &task) { return task.task.server == server && task.token == progress.token; });
    switch (progress.kind) {
    case Kind::Begin:
        if (tracked != tasks_.end())
            return; // a token begins once
        tasks_.append({progress.token, {server, progress.title, progress.message, progress.percentage}});
        break;
    case Kind::Report:
        if (tracked == tasks_.end())
            return;
        // A report leaves out what did not change.
        if (!progress.message.isEmpty())
            tracked->task.message = progress.message;
        if (progress.percentage)
            tracked->task.percentage = progress.percentage;
        break;
    case Kind::End:
        if (tracked == tasks_.end())
            return;
        tasks_.erase(tracked);
        break;
    }
    emit tasksChanged();
}

void LanguageServers::forgetTasks(const QString &server)
{
    if (tasks_.removeIf([&](const TrackedTask &task) { return task.task.server == server; }) > 0)
        emit tasksChanged();
}

void LanguageServers::forgetDocuments(const QString &server)
{
    for (auto it = documents_.begin(); it != documents_.end();) {
        if (it.value().server == server)
            it = documents_.erase(it);
        else
            ++it;
    }
}

namespace {

// Completion as in IntelliJ IDEA.
QJsonObject jdtlsSettings()
{
    return QJsonObject{
        {"completion",
         QJsonObject{
             // The static members offered, and imported, without their
             // class typed first ("assertEquals", "toList").
             {"favoriteStaticMembers",
              QJsonArray{"org.junit.Assert.*", "org.junit.Assume.*", "org.junit.jupiter.api.Assertions.*",
                         "org.junit.jupiter.api.Assumptions.*", "org.junit.jupiter.api.DynamicContainer.*",
                         "org.junit.jupiter.api.DynamicTest.*", "org.mockito.Mockito.*",
                         "org.mockito.ArgumentMatchers.*", "org.assertj.core.api.Assertions.*",
                         "org.hamcrest.MatcherAssert.*", "org.hamcrest.Matchers.*", "java.util.stream.Collectors.*",
                         "java.util.Objects.requireNonNull", "java.util.Objects.requireNonNullElse"}},
             // Never offered: "List" is java.util's, not java.awt's.
             {"filteredTypes", QJsonArray{"java.awt.*", "com.sun.*", "sun.*", "jdk.*", "org.graalvm.*",
                                          "io.micrometer.shaded.*", "javax.swing.text.html.HTML.Tag"}},
             // A method comes with its parameters' names to fill in.
             {"guessMethodArguments", "insertParameterNames"},
             // Templates are Duet's own (see PostfixTemplateProvider), not
             // twice in the list.
             {"postfix", QJsonObject{{"enabled", false}}},
             // Values reached through a chain of calls, for the expected
             // type ("list.stream().toList()").
             {"chain", QJsonObject{{"enabled", true}}},
             // More than jdtls's 50: typing further narrows the list
             // without asking again.
             {"maxResults", 100},
             // As IntelliJ: java.*, javax.*, then the others.
             {"importOrder", QJsonArray{"java", "javax", "org", "com", ""}},
         }},
        // The code it generates (overrides, getters) uses braces.
        {"codeGeneration", QJsonObject{{"useBlocks", true}}},
    };
}

// The completion preferences of tsserver, as in WebStorm: the exports of
// every module, imported once chosen; class members and object methods
// written whole; "?." added where a value may be undefined.
QJsonObject typescriptInitialization()
{
    return QJsonObject{
        {"preferences",
         QJsonObject{
             {"includeCompletionsForModuleExports", true},
             {"includeCompletionsForImportStatements", true},
             {"includeCompletionsWithSnippetText", true},
             {"includeCompletionsWithInsertText", true},
             {"includeAutomaticOptionalChainCompletions", true},
             {"includeCompletionsWithClassMemberSnippets", true},
             {"includeCompletionsWithObjectLiteralMethodSnippets", true},
             {"useLabelDetailsInCompletionEntries", true},
             {"allowIncompleteCompletions", true},
             {"importModuleSpecifierPreference", "shortest"},
             {"importModuleSpecifierEnding", "auto"},
             {"jsxAttributeCompletionStyle", "auto"},
         }},
    };
}

} // namespace

LanguageServers::Options LanguageServers::optionsFor(const QString &server,
                                                     const LanguageSetup &setup,
                                                     const QString &runtimeHome,
                                                     const QString &runtimeExecutable)
{
    Options options;
    if (server == QLatin1String("clangd")) {
        // For the files without compile commands.
        if (setup.runtimeVersion.startsWith(QLatin1String("c++")))
            options.initialization.insert("fallbackFlags", QJsonArray{QStringLiteral("-std=") + setup.runtimeVersion});
    } else if (server == QLatin1String("jdtls")) {
        QJsonObject java = jdtlsSettings();
        if (!runtimeHome.isEmpty()) {
            // The project's JDK, by its execution environment's name.
            const int major = setup.runtimeVersion.toInt();
            const QString environment =
                major <= 8 ? QStringLiteral("JavaSE-1.%1").arg(major) : QStringLiteral("JavaSE-%1").arg(major);
            java.insert("configuration", QJsonObject{{"runtimes", QJsonArray{QJsonObject{{"name", environment},
                                                                                         {"path", runtimeHome},
                                                                                         {"default", true}}}}});
        }
        options.settings = QJsonObject{{"java", java}};
        options.initialization.insert("settings", options.settings);
    } else if (server == QLatin1String("typescript-language-server")) {
        options.initialization = typescriptInitialization();
        // "name(…)" with its arguments to fill in, as a function is
        // inserted in IntelliJ.
        options.settings = QJsonObject{{"completions", QJsonObject{{"completeFunctionCalls", true}}}};
    } else if (server == QLatin1String("pyright")) {
        if (!runtimeExecutable.isEmpty())
            options.settings = QJsonObject{{"python", QJsonObject{{"pythonPath", runtimeExecutable}}}};
    } else if (server == QLatin1String("pylsp") && !runtimeExecutable.isEmpty()) {
        options.settings = QJsonObject{
            {"pylsp",
             QJsonObject{{"plugins", QJsonObject{{"jedi", QJsonObject{{"environment", runtimeExecutable}}}}}}}};
    }

    return options;
}

void LanguageServers::setRequestHandler(const QString &method, const LspClient::RequestHandler &handler)
{
    requestHandlers_.insert(method, handler);
    for (LspClient *client : std::as_const(clients_))
        client->setRequestHandler(method, handler);
}

bool LanguageServers::syncDocument(LspClient *client,
                                   const QString &path,
                                   const QString &languageId,
                                   const QByteArray &text)
{
    const QString server = clients_.key(client);
    const QString uri = QUrl::fromLocalFile(path).toString();
    const size_t hash = qHash(text);
    const auto it = documents_.find(path);
    if (it == documents_.end() || it.value().server != server) {
        documents_.insert(path, {server, 1, hash});
        client->notify(QStringLiteral("textDocument/didOpen"),
                       QJsonObject{{"textDocument", QJsonObject{{"uri", uri},
                                                                {"languageId", protocolLanguage(languageId)},
                                                                {"version", 1},
                                                                {"text", QString::fromUtf8(text)}}}});
        return true;
    }
    if (it.value().hash == hash)
        return false;
    it.value().hash = hash;
    const int version = ++it.value().version;
    // The whole text: simple, and fast enough for source files.
    client->notify(QStringLiteral("textDocument/didChange"),
                   QJsonObject{{"textDocument", QJsonObject{{"uri", uri}, {"version", version}}},
                               {"contentChanges", QJsonArray{QJsonObject{{"text", QString::fromUtf8(text)}}}}});
    return true;
}

bool LanguageServers::isCurrent(const QString &path, int version, const QByteArray &text) const
{
    const auto it = documents_.constFind(path);
    if (it == documents_.cend())
        return true;
    return it.value().version == version && it.value().hash == qHash(text);
}

void LanguageServers::closeDocument(const QString &path)
{
    const auto it = documents_.constFind(path);
    if (it == documents_.cend())
        return;
    if (LspClient *client = clients_.value(it.value().server))
        client->notify(QStringLiteral("textDocument/didClose"),
                       QJsonObject{{"textDocument", QJsonObject{{"uri", QUrl::fromLocalFile(path).toString()}}}});
    documents_.erase(it);
}

QStringList LanguageServers::filesWithDiagnostics() const
{
    QStringList files;
    for (auto it = diagnostics_.cbegin(); it != diagnostics_.cend(); ++it) {
        if (!it.value().isEmpty())
            files.append(it.key());
    }
    files.sort();
    return files;
}

void LanguageServers::warmUp(const QString &path, const QString &languageId, const QByteArray &text)
{
    if (path.isEmpty())
        return;
    LspClient *client = clientFor(languageId);
    if (!client)
        return;
    syncDocument(client, path, languageId, text);
    const QString server = clients_.key(client);
    if (warmed_.contains(server))
        return;
    warmed_.insert(server);
    // Sent once the server is initialized, after the file.
    client->request(QStringLiteral("textDocument/completion"),
                    QJsonObject{{"textDocument", QJsonObject{{"uri", QUrl::fromLocalFile(path).toString()}}},
                                {"position", QJsonObject{{"line", 0}, {"character", 0}}},
                                {"context", QJsonObject{{"triggerKind", 1}}}},
                    [](const QJsonValue &, const QString &) { });
}
