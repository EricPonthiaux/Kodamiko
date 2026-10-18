#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QStringList>

#include <functional>

// A language server (clangd, jdtls...) run as a process, spoken to in the
// Language Server Protocol: JSON-RPC messages with a Content-Length header
// on its standard input and output. Requests sent before the server has
// answered "initialize" wait for it.
class LspClient : public QObject {
    Q_OBJECT

public:
    using ResponseHandler = std::function<void(const QJsonValue &result, const QString &error)>;
    // Answers a request from the server ("workspace/applyEdit"...).
    using RequestHandler = std::function<QJsonValue(const QJsonValue &params)>;

    // `program` and `arguments` start the server; `rootFolder` is the
    // workspace it works on.
    LspClient(QString program, QStringList arguments, const QString &rootFolder, QObject *parent = nullptr);
    // Before start(): the server's environment (JAVA_HOME for jdtls...).
    void setEnvironment(const QProcessEnvironment &environment);

    // Before start(): the "initializationOptions" of "initialize".
    void setInitializationOptions(const QJsonObject &options) { initializationOptions_ = options; }

    // The workspace settings ({"python": {"pythonPath": ...}}): the answer
    // to the server's "workspace/configuration" requests, also sent once
    // it is initialized.
    void setSettings(const QJsonObject &settings) { settings_ = settings; }

    // How to answer the server's requests of `method`; the others get null.
    void setRequestHandler(const QString &method, const RequestHandler &handler)
    {
        requestHandlers_.insert(method, handler);
    }

    // Asks the server to stop, and kills it if it does not.
    ~LspClient() override;

    void start();
    bool isRunning() const;

    bool isReady() const { return ready_; }

    bool hasFailed() const { return failed_; }

    QString program() const { return program_; }

    // The server's capabilities, from its answer to "initialize".
    QJsonObject capabilities() const { return capabilities_; }

    // Sends a request; `handler` gets its result, or an error message.
    void request(const QString &method, const QJsonObject &params, ResponseHandler handler);
    void notify(const QString &method, const QJsonObject &params);

    // Splits the complete messages off the front of `buffer`, leaving an
    // incomplete one there. Public for tests.
    static QList<QJsonObject> takeMessages(QByteArray &buffer);
    static QByteArray frame(const QJsonObject &message);
    // The value of `section` ("python.analysis") in `settings`; the whole
    // settings for an empty section, null when absent.
    static QJsonValue settingAt(const QJsonObject &settings, const QString &section);

signals:
    void ready();
    // A notification from the server ("textDocument/publishDiagnostics").
    void notificationReceived(const QString &method, const QJsonValue &params);
    // The process stopped or could not start.
    void failed(const QString &message);

private:
    void send(const QJsonObject &message);
    void onOutput();
    void handle(const QJsonObject &message);

    QString program_;
    QStringList arguments_;
    QString rootFolder_;
    QProcess *process_; // outlives this object while the server stops
    QByteArray buffer_;
    // The end of the server's error output: why it stopped.
    QByteArray errors_;
    QString lastError() const;
    int nextId_ = 1;
    QHash<int, ResponseHandler> pending_;
    QList<QJsonObject> waiting_; // sent once initialized
    QJsonObject capabilities_;
    QJsonObject initializationOptions_;
    QJsonObject settings_;
    QHash<QString, RequestHandler> requestHandlers_;
    bool ready_ = false;
    bool failed_ = false;
    bool stopping_ = false;
};
