// SPDX-License-Identifier: GPL-3.0-only

// A language server for tests, which answers on its standard output:
// - "initialize": no capabilities;
// - "test/echo": its params;
// - "test/silent": never;
// - "test/progress": after a "$/progress" notification for each of its
//   "values", with its "token";
// - "$/cancelRequest": a "test/cancelled" notification with the id, so
//   that a test sees the cancellation arrive;
// - "exit": stops; any other request: null.

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <array>
#include <cstdio>
#include <optional>
#include <string_view>

namespace {

// The body of the next message on standard input; nothing at its end.
std::optional<QByteArray> readMessage()
{
    long long length = -1;
    std::array<char, 256> line{};
    while (std::fgets(line.data(), int(line.size()), stdin)) {
        const QByteArray header = QByteArray(line.data()).trimmed();
        if (header.isEmpty())
            break;
        if (header.toLower().startsWith("content-length:"))
            length = header.mid(int(std::string_view("content-length:").size())).trimmed().toLongLong();
    }
    if (length < 0)
        return std::nullopt;
    QByteArray body(length, Qt::Uninitialized);
    if (std::fread(body.data(), 1, size_t(length), stdin) != size_t(length))
        return std::nullopt;
    return body;
}

void send(const QJsonObject &message)
{
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    const QByteArray framed = "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    std::fwrite(framed.constData(), 1, size_t(framed.size()), stdout);
    std::fflush(stdout);
}

void answer(const QJsonObject &request, const QJsonValue &result)
{
    send(QJsonObject{{"jsonrpc", "2.0"}, {"id", request.value("id")}, {"result", result}});
}

void sendProgress(const QJsonObject &params)
{
    for (const QJsonValue &value : params.value("values").toArray())
        send(QJsonObject{{"jsonrpc", "2.0"},
                         {"method", "$/progress"},
                         {"params", QJsonObject{{"token", params.value("token")}, {"value", value}}}});
}

} // namespace

int main()
{
    while (const std::optional<QByteArray> body = readMessage()) {
        const QJsonObject message = QJsonDocument::fromJson(*body).object();
        const QString method = message.value("method").toString();
        if (method == QLatin1String("exit"))
            return 0;
        if (method == QLatin1String("$/cancelRequest")) {
            send(QJsonObject{{"jsonrpc", "2.0"}, {"method", "test/cancelled"}, {"params", message.value("params")}});
            continue;
        }
        const bool isRequest = message.contains("id");
        if (!isRequest || method == QLatin1String("test/silent"))
            continue;
        if (method == QLatin1String("initialize")) {
            answer(message, QJsonObject{{"capabilities", QJsonObject{}}});
        } else if (method == QLatin1String("test/echo")) {
            answer(message, message.value("params"));
        } else if (method == QLatin1String("test/progress")) {
            sendProgress(message.value("params").toObject());
            answer(message, QJsonValue::Null);
        } else {
            answer(message, QJsonValue::Null);
        }
    }
    return 0;
}
