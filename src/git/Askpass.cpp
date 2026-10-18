#include "Askpass.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QPointer>
#include <QTimer>
#include <QUuid>

namespace {

QString defaultHelperPath()
{
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("duet-askpass.exe");
#else
    const QString name = QStringLiteral("duet-askpass");
#endif
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(name);
    return QFileInfo::exists(path) ? path : QString();
}

} // namespace

AskpassServer::AskpassServer(Prompter prompter, QObject *parent)
    : QObject(parent)
    , prompter_(std::move(prompter))
    , helperPath_(defaultHelperPath())
{
    connect(&server_, &QLocalServer::newConnection, this, &AskpassServer::onNewConnection);
}

bool AskpassServer::listen()
{
    // Only the user may connect: the answers are secrets.
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    return server_.listen(QStringLiteral("duet-askpass-") + QUuid::createUuid().toString(QUuid::Id128));
}

QHash<QString, QString> AskpassServer::environment() const
{
    if (helperPath_.isEmpty() || !server_.isListening())
        return {};
    return {
        {QStringLiteral("GIT_ASKPASS"), helperPath_},
        {QStringLiteral("SSH_ASKPASS"), helperPath_},
        // ssh uses SSH_ASKPASS even with no display and with a terminal
        // (OpenSSH 8.4 and later).
        {QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force")},
        {QStringLiteral("DUET_ASKPASS_SOCKET"), server_.fullServerName()},
    };
}

bool AskpassServer::isConfirmation(const QString &prompt)
{
    return prompt.contains(QLatin1String("(yes/no"));
}

void AskpassServer::onNewConnection()
{
    while (QLocalSocket *socket = server_.nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            if (!socket->canReadLine())
                return;
            const QByteArray request = socket->readLine();
            // Not from within the socket's signal: the prompt is a modal
            // dialog, with its own event loop.
            QTimer::singleShot(0, this, [this, socket = QPointer<QLocalSocket>(socket), request] {
                if (socket)
                    answer(socket, request);
            });
        });
    }
}

void AskpassServer::answer(QLocalSocket *socket, const QByteArray &request)
{
    const QString prompt = QJsonDocument::fromJson(request).object().value(QLatin1String("prompt")).toString();
    const QPointer<QLocalSocket> guard(socket);
    const std::optional<QString> answer = prompter_(prompt);
    // git gave up waiting (cancelled) while the user was typing.
    if (!guard)
        return;
    const QJsonObject reply =
        answer ? QJsonObject{{QLatin1String("answer"), *answer}} : QJsonObject{{QLatin1String("cancelled"), true}};
    socket->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();
}
