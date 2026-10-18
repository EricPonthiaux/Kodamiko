#include "FullLine.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {

// Tokens asked for: the rest of a line.
constexpr int maxTokens = 64;
// A local model that takes longer is too slow to follow the typing.
constexpr int timeoutMs = 5000;

} // namespace

FullLineModel::FullLineModel(QObject *parent)
    : QObject(parent)
{
}

FullLineModel::~FullLineModel() = default;

// The virtual signature takes the callback by value, for the overrides that store it.
// NOLINTNEXTLINE(performance-unnecessary-value-param)
void FullLineModel::suggest(const QString &, const QString &, const QString &, Callback done)
{
    done({});
}

void FullLineModel::cancel()
{
}

QString FullLineModel::lineOf(const QString &completion, const QString &after)
{
    QString line = completion.section(QLatin1Char('\n'), 0, 0);
    line.remove(QLatin1Char('\r'));
    while (!line.isEmpty() && line.back().isSpace())
        line.chop(1);
    // What follows the cursor already ("}" or ");" closed by the editor).
    const QString rest = after.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (!rest.isEmpty() && line.endsWith(rest))
        line.chop(rest.size());
    while (!line.isEmpty() && line.back().isSpace())
        line.chop(1);
    return line.trimmed().isEmpty() ? QString() : line;
}

LocalFullLineModel::LocalFullLineModel(QObject *parent)
    : FullLineModel(parent)
    , network_(new QNetworkAccessManager(this))
{
}

void LocalFullLineModel::configure(Server server, const QUrl &endpoint, const QString &model)
{
    cancel();
    server_ = server;
    endpoint_ = endpoint;
    model_ = model;
    failing_ = false;
}

void LocalFullLineModel::cancel()
{
    if (pending_) {
        QNetworkReply *reply = pending_;
        pending_ = nullptr;
        reply->abort();
    }
}

void LocalFullLineModel::suggest(const QString &before, const QString &after, const QString &, Callback done)
{
    cancel();
    if (!endpoint_.isValid()) {
        done({});
        return;
    }
    QUrl url = endpoint_;
    QString path = url.path();
    if (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    QJsonObject body;
    if (server_ == Server::Ollama) {
        url.setPath(path + QStringLiteral("/api/generate"));
        body = {
            {"model", model_},
            {"prompt", before},
            {"suffix", after},
            {"stream", false},
            {"keep_alive", "15m"},
            {"options", QJsonObject{{"temperature", 0}, {"num_predict", maxTokens}, {"stop", QJsonArray{"\n"}}}},
        };
    } else {
        url.setPath(path + QStringLiteral("/infill"));
        body = {
            {"input_prefix", before}, {"input_suffix", after},    {"n_predict", maxTokens},
            {"temperature", 0},       {"stop", QJsonArray{"\n"}}, {"cache_prompt", true},
        };
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(timeoutMs);
    QNetworkReply *reply = network_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    pending_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, after, done] {
        reply->deleteLater();
        if (pending_ != reply)
            return; // cancelled, or a newer request
        pending_ = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            if (!failing_ && reply->error() != QNetworkReply::OperationCanceledError) {
                failing_ = true;
                emit failed(tr("Full Line completion: %1").arg(reply->errorString()));
            }
            done({});
            return;
        }
        failing_ = false;
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
        const QString text = answer.value(server_ == Server::Ollama ? "response" : "content").toString();
        done(lineOf(text, after));
    });
}
