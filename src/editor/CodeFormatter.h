// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Formatting a file's text with the external formatter of its language
// (clang-format, prettier, ruff, black, shfmt, cmake-format). The text goes
// through the formatter's standard input, so unsaved changes are formatted
// too; the formatter still knows the file's path, to find the project's
// configuration (.clang-format, .prettierrc, pyproject.toml...).
namespace CodeFormatter {

struct Command {
    QString program; // name, or path of a project-local install
    QStringList arguments;
};

// Formatters able to handle `languageId` (a LanguageRegistry identifier),
// preferred first, whether they are installed or not.
QList<Command> candidates(const QString &languageId, const QString &filePath);

// First installed candidate, with `program` turned into an absolute path;
// nullopt when there is none. A prettier installed in a node_modules folder
// above the file comes before one in the PATH.
std::optional<Command> find(const QString &languageId, const QString &filePath);

// Names of the candidates, for a message about a missing formatter
// ("clang-format or prettier").
QString describeCandidates(const QString &languageId, const QString &filePath);

// Runs `command` in `workDir` with `text` as its input. Returns the
// formatted text, or nullopt with the formatter's error message.
std::optional<QString> run(const Command &command, const QString &workDir, const QString &text, QString *error);

// Same as run(), without blocking, for `job.command` run in `job.workDir`
// on `job.text`: `done` is called later, on `context`'s
// thread, with the formatted text, or nullopt and the error message. It is
// not called if `context` is destroyed first.
struct Job {
    Command command;
    QString workDir;
    QString text;
};

using Done = std::function<void(const std::optional<QString> &formatted, const QString &error)>;
void start(const Job &job, const QObject *context, const Done &done);

// Where `position` (an offset in `before`) ends up in `after`: at the same
// number of non-blank characters from the start. Formatters mostly move
// blanks around, so the cursor stays next to the same code.
int mapPosition(const QByteArray &before, int position, const QByteArray &after);

} // namespace CodeFormatter
