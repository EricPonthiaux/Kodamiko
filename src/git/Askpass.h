#pragma once

#include <QHash>
#include <QLocalServer>
#include <QObject>

#include <functional>
#include <optional>

class QLocalSocket;

// Asks the user for the credentials git and ssh need (password, key
// passphrase, unknown host key) while a fetch, pull or push runs.
//
// git and ssh run the small `duet-askpass` program (GIT_ASKPASS,
// SSH_ASKPASS) with the prompt as argument; it relays the prompt over a
// local socket to this server, which asks the user and sends back the
// answer, printed by duet-askpass for git to read.
//
// Protocol: duet-askpass sends one JSON line {"prompt": "..."}; the server
// replies with one JSON line, {"answer": "..."} or {"cancelled": true}.
// Nothing is remembered here: the user's credential helper does that.
class AskpassServer : public QObject {
    Q_OBJECT

public:
    // Asks the user; returns the answer, or nothing when cancelled. Runs on
    // the UI thread.
    using Prompter = std::function<std::optional<QString>(const QString &prompt)>;

    explicit AskpassServer(Prompter prompter, QObject *parent = nullptr);

    bool listen();
    // The variables that make git and ssh ask through this server; empty
    // when duet-askpass cannot be found or the server is not listening.
    QHash<QString, QString> environment() const;

    // The duet-askpass program; by default, the one next to the editor.
    void setHelperPath(const QString &path) { helperPath_ = path; }

    // Whether `prompt` asks a yes/no question (ssh's unknown host key)
    // rather than for a secret.
    static bool isConfirmation(const QString &prompt);

private:
    void onNewConnection();
    void answer(QLocalSocket *socket, const QByteArray &request);

    Prompter prompter_;
    QLocalServer server_;
    QString helperPath_;
};
