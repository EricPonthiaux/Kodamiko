// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class LanguageServers;

// The errors an agent's edit brought into the files it edited, as the
// language servers report them, told to the agent right after the edit
// (FilesEdited hook): it fixes a type error at once instead of finding it
// at the next build.
namespace EditErrors {

// The errors of one edited file, LSP Diagnostic objects.
struct FileErrors {
    QString path; // as the agent reads it: relative to the project when inside it
    QJsonArray errors;
};

// Longest list told for one file; the rest is counted.
constexpr int maximumErrorsPerFile = 10;

// The errors among `after`'s diagnostics that `before`'s do not have: the
// same message, wherever it is, is the same error moved by the edit.
[[nodiscard]] QJsonArray introduced(const QJsonArray &before, const QJsonArray &after);
// The errors among `diagnostics`.
[[nodiscard]] QJsonArray errorsAmong(const QJsonArray &diagnostics);
// What the agent reads: a line per error, "src/a.cpp:42:7: message
// (clangd)"; empty when no file has errors.
[[nodiscard]] QString describe(const QList<FileErrors> &files);

} // namespace EditErrors

// Checks the files an agent edits with the language servers already
// running for them: none is started for a check, which would keep the
// agent waiting while it loads the project.
class EditErrorCheck : public QObject {
    Q_OBJECT

public:
    // `servers` is not owned and outlives the check.
    explicit EditErrorCheck(LanguageServers *servers, QObject *parent = nullptr);

    // Before the agent edits `paths` (absolute, EditRequested hook): the
    // errors the servers report in them now, those the edit does not bring.
    void rememberErrorsBefore(const QStringList &paths);

    using Done = std::function<void(const QString &report)>;
    // After the agent edited `paths` (FilesEdited hook): sends their new text
    // to their servers, waits for their problems, then calls `done` with
    // EditErrors::describe() of the errors the edit brought. The report is
    // empty when there are none, when no server runs for the files, or for
    // the files whose server did not answer in time. `done` is called at
    // once when there is nothing to wait for.
    void checkAfterEdit(const QStringList &paths, Done done);

private:
    LanguageServers *servers_;
    // By absolute path, from rememberErrorsBefore() to checkAfterEdit().
    QHash<QString, QJsonArray> errorsBefore_;
};
