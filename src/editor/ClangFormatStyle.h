// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QString>

#include <functional>
#include <optional>

class QObject;

// The layout clang-format gives a file: the project's .clang-format, or
// clang-format's default style without one. The editor indents as the
// formatter will, so that typing and formatting agree.
namespace ClangFormatStyle {

struct Style {
    bool useTabs = false;
    int indentWidth = 4;
    int tabWidth = 4;
    int columnLimit = 0; // 0: no limit
};

// Whether clang-format formats files of `languageId` (a LanguageRegistry
// identifier) in Duet.
bool appliesTo(const QString &languageId);

// The style in the output of `clang-format --dump-config`; nullopt if it
// lacks the indentation width.
std::optional<Style> parse(const QByteArray &dumpedConfig);

// Asks clang-format, without blocking, for the style of `filePath`. `done`
// is called with nullopt when clang-format is not installed or fails, never
// after `context` is destroyed.
void load(const QString &filePath, QObject *context, const std::function<void(std::optional<Style>)> &done);

} // namespace ClangFormatStyle
