// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QMap>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

// Reading the environment a developer prompt sets (VisualStudio: the x64
// Native Tools prompt of Windows).
namespace DeveloperEnvironment {

// The variables of `setOutput` (cmd's `set`, one NAME=value per line) that
// `before` lacks or has with another value.
QMap<QString, QString> changedVariables(const QByteArray &setOutput, const QProcessEnvironment &before);

} // namespace DeveloperEnvironment
