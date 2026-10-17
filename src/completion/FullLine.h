#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// Full Line completion, as in CLion: a code model running on this machine
// suggests the rest of the line, shown in gray after the cursor. The
// model is served by Ollama or by llama.cpp's server, asked to fill in
// the middle: the code before the cursor and the code after it.
class FullLineModel : public QObject {
    Q_OBJECT

public:
    using Callback = std::function<void(const QString &suggestion)>;

    explicit FullLineModel(QObject *parent = nullptr);
    ~FullLineModel() override;

    // Asks for the rest of the line at the cursor. `done` gets the
    // suggestion (possibly empty), unless cancelled or outdated by a newer
    // request.
    virtual void suggest(const QString &before, const QString &after, const QString &languageId, Callback done);
    virtual void cancel();

    // The rest of the line of `completion`, as shown: the first line
    // only, without what already follows the cursor (`after`), nor
    // trailing blanks. Public for tests.
    static QString lineOf(const QString &completion, const QString &after);
};

// A model served over HTTP on this machine.
class LocalFullLineModel : public FullLineModel {
    Q_OBJECT

public:
    enum class Server {
        Ollama,   // POST /api/generate with "prompt" and "suffix"
        LlamaCpp, // POST /infill with "input_prefix" and "input_suffix"
    };

    explicit LocalFullLineModel(QObject *parent = nullptr);

    void configure(Server server, const QUrl &endpoint, const QString &model);

    void suggest(const QString &before, const QString &after, const QString &languageId, Callback done) override;
    void cancel() override;

signals:
    // The server could not be reached or refused: said once until it
    // answers again.
    void failed(const QString &message);

private:
    QNetworkAccessManager *network_;
    QPointer<QNetworkReply> pending_;
    Server server_ = Server::Ollama;
    QUrl endpoint_;
    QString model_;
    bool failing_ = false;
};
