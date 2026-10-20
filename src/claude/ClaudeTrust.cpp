#include "ClaudeTrust.h"

#include "ClaudeConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSaveFile>

#include <optional>

namespace {

constexpr auto projectsKey = "projects";
constexpr auto trustedKey = "hasTrustDialogAccepted";

// The key Claude Code files `folder` under: its absolute path, symbolic
// links resolved, with forward slashes.
QString keyOf(const QString &folder)
{
    const QFileInfo info(folder);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

// The state file's object; an empty one when the file does not exist yet,
// nullopt (with `error` set) when it is not a JSON object.
std::optional<QJsonObject> readState(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.exists())
        return QJsonObject();
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("Cannot read %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        *error =
            QObject::tr("%1 is not a JSON object: %2").arg(QDir::toNativeSeparators(path), parseError.errorString());
        return std::nullopt;
    }
    return document.object();
}

bool writeState(const QString &path, const QJsonObject &state, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    // Written to a temporary file then renamed: a Claude session reading
    // the file meanwhile never sees half of it.
    QSaveFile file(path);
    const QByteArray json = QJsonDocument(state).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
        *error = QObject::tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}

} // namespace

namespace ClaudeTrust {

bool isTrusted(const QString &folder)
{
    QString error;
    const std::optional<QJsonObject> state = readState(ClaudeConfig::stateFile(), &error);
    if (!state)
        return false;
    return state->value(QLatin1String(projectsKey))
        .toObject()
        .value(keyOf(folder))
        .toObject()
        .value(QLatin1String(trustedKey))
        .toBool();
}

bool trust(const QString &folder, QString *error)
{
    if (isTrusted(folder))
        return true;
    const QString path = ClaudeConfig::stateFile();
    std::optional<QJsonObject> state = readState(path, error);
    if (!state)
        return false;
    QJsonObject projects = state->value(QLatin1String(projectsKey)).toObject();
    QJsonObject project = projects.value(keyOf(folder)).toObject();
    project.insert(QLatin1String(trustedKey), true);
    projects.insert(keyOf(folder), project);
    state->insert(QLatin1String(projectsKey), projects);
    return writeState(path, *state, error);
}

} // namespace ClaudeTrust
