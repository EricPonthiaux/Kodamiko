// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ClaudeConfig.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

// The settings Claude Code applies once it has merged its settings files,
// each value with the file it comes from: the Claude pane's "Effective
// Settings".
//
// Files are merged from the lowest precedence to the highest (see
// ClaudeConfig::settingsFiles): objects key by key, arrays put together
// without duplicates (permissions, hooks...), any other value replaced by
// the later file's.
namespace EffectiveSettings {

struct Layer {
    ClaudeConfig::Scope scope = ClaudeConfig::Scope::User;
    QString path;
    bool exists = false;
    QString error; // the file is not a JSON object: ignored
    QJsonObject settings;
};

// The settings files of `projectDir`, read, lowest precedence first.
QList<Layer> read(const QString &projectDir, const QString &sessionSettings);

// The merged settings, without their origin.
QJsonObject merged(const QList<Layer> &layers);

// The merged settings as commented JSON (JSONC): a header listing the
// files, then every value followed by "// Scope", and by the values it
// replaced ("// Local, over User: \"sonnet\"").
QString render(const QList<Layer> &layers, const QString &projectDir);

} // namespace EffectiveSettings
