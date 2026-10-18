#pragma once

#include "LspClient.h"
#include "projects/ProjectConfig.h"
#include "toolchains/JavaRuntimes.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <optional>

class Toolchains;

// A language server Duet knows how to run.
struct LanguageServerSpec {
    QString name;    // "clangd"
    QString program; // looked up in the PATH
    QStringList arguments;
    QStringList languages;     // LanguageRegistry identifiers it serves
    QString triggerCharacters; // characters after which it completes members
};

// The language servers of the open folder: one per kind (clangd for C and
// C++, jdtls for Java...), started the first time a file of its languages
// needs it, and stopped when the folder changes. Keeps each server's copy
// of the open files up to date. With a project's setup, each language uses
// the server and version it chose (Duet's download, or the PATH's), run
// for the project's runtime (its JDK, Python, C++ standard).
class LanguageServers : public QObject {
    Q_OBJECT

public:
    explicit LanguageServers(QObject *parent = nullptr);
    ~LanguageServers() override;

    // Every server Duet knows, installed or not.
    static const QList<LanguageServerSpec> &known();
    // Protocol identifier of a LanguageRegistry language ("typescriptreact"
    // for "tsx").
    static QString protocolLanguage(const QString &languageId);

    // Stops the servers: they worked on the previous folder. Without a
    // project setup: the first server of each language in the PATH.
    void setFolder(const QString &folder);
    // The project's folder and setup; `toolchains` finds Duet's downloads
    // and the runtimes. Stops the servers if either changed.
    void setProject(const ProjectConfig &project, Toolchains *toolchains);
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }

    // The installed server for `languageId`, if any.
    std::optional<LanguageServerSpec> specFor(const QString &languageId) const;
    // Names of the installed servers ("clangd", "jdtls 1.62.0").
    QStringList installed() const;
    // The server for `languageId`, started on first use; nullptr when
    // there is none, it is disabled, or it failed.
    LspClient *clientFor(const QString &languageId);
    // The server for `languageId` if it is already running, without
    // starting it.
    LspClient *runningClientFor(const QString &languageId) const;
    // Why the server of `languageId` cannot run ("jdtls needs Java 21..."),
    // empty if it can (or was not tried yet).
    QString failure(const QString &languageId) const;
    // Forgets the failure of the server of `languageId`: the next
    // completion starts it again (a JDK was installed meanwhile).
    void retry(const QString &languageId);
    // Lines describing each installed server, its state or problem, for the
    // preferences.
    QStringList describe() const;

    // The open folder, the servers' workspace.
    QString folder() const { return folder_; }
    // How every server, started now or later, answers the requests of
    // `method` it sends ("workspace/applyEdit").
    void setRequestHandler(const QString &method, LspClient::RequestHandler handler);

    // Sends `text` as the content of `path` to `client`: didOpen the first
    // time, didChange when it changed since.
    // Returns whether anything was sent (the text was new to the server).
    bool syncDocument(LspClient *client, const QString &path, const QString &languageId, const QByteArray &text);

    // The problems the servers last reported for `path` (LSP Diagnostic
    // objects), and the files they reported problems for.
    QJsonArray diagnostics(const QString &path) const { return diagnostics_.value(path); }
    QStringList filesWithDiagnostics() const;
    // Starts the server of `languageId` before the first completion needs
    // it, and gives it `path` to read: clangd builds the file's preamble,
    // jdtls imports the project, tsserver loads its libraries. The first
    // time for each server, a completion is also asked at the start of the
    // file, its answer dropped: what the user's first one would wait for
    // is then loaded.
    void warmUp(const QString &path, const QString &languageId, const QByteArray &text);

    // How jdtls is started: its own Java command (no Python needed), with
    // `java`, a JDK of version 21 or more. nullopt, with `error`, if
    // impossible.
    struct Launch {
        QString program;
        QStringList arguments;
        QString javaHome;
    };
    static std::optional<Launch> jdtlsLaunch(const QString &jdtlsProgram, const QString &dataFolder,
                                             const std::optional<JavaRuntime> &java, QString *error);

    // What a server is told about the project's setup: its
    // initializationOptions and its workspace settings. Public for tests.
    struct Options {
        QJsonObject initialization;
        QJsonObject settings;
    };
    // `runtimeHome` and `runtimeExecutable` are the project's JDK or Python.
    static Options optionsFor(const QString &server, const LanguageSetup &setup, const QString &runtimeHome,
                              const QString &runtimeExecutable);

signals:
    void statusMessage(const QString &message);
    // A server reported the problems of `path` (possibly none).
    void diagnosticsChanged(const QString &path);
    // The server of `languages` could not start or stopped; `message` says
    // why. Also emitted, with an empty message, when it is retried.
    void serverFailed(const QString &server, const QStringList &languages, const QString &message);

private:
    void stopAll();
    QString programPath(const QString &program) const;
    // The project's setup of `languageId`'s family, if it has one.
    const LanguageSetup *setupFor(const QString &languageId) const;
    // The command of `spec` for the project: Duet's download of the chosen
    // version, else the PATH's. Empty when there is none.
    QString programFor(const LanguageServerSpec &spec) const;
    std::optional<LanguageServerSpec> findSpec(const QString &languageId) const;

    QString folder_;
    ProjectConfig project_;
    Toolchains *toolchains_ = nullptr;
    bool enabled_ = true;
    QHash<QString, LspClient *> clients_;                       // by server name
    QHash<QString, LspClient::RequestHandler> requestHandlers_; // by method
    QHash<QString, QString> failures_;                          // by server name
    struct Document {
        QString server;
        int version = 0;
        size_t hash = 0;
    };
    QHash<QString, Document> documents_;           // by path
    QHash<QString, QJsonArray> diagnostics_;       // by path
    QSet<QString> warmed_;                         // servers already asked a completion, by name
    mutable QHash<QString, QString> programPaths_; // lookups in the PATH
    // specFor() by language: asked at every typed character (trigger
    // characters), and it looks for programs on disk.
    mutable QHash<QString, std::optional<LanguageServerSpec>> specs_;
};
