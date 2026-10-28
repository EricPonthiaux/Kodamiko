// SPDX-License-Identifier: GPL-3.0-only

#include "EditErrors.h"

#include "editor/TextFile.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspProtocol.h"
#include "syntax/LanguageRegistry.h"

#include <QDir>
#include <QJsonObject>
#include <QSet>
#include <QTimer>

#include <optional>
#include <utility>

namespace {

// How long a server may take to report the problems of an edited file. The
// agent waits for it before going on: clangd needs about a second for a
// source file, more when a header changed.
constexpr int diagnosticsWaitMs = 3000;

constexpr int lspErrorSeverity = 1;

QString messageOf(const QJsonValue &diagnostic)
{
    return diagnostic.toObject().value(QLatin1String("message")).toString().simplified();
}

// "src/a.cpp:42:7: use of undeclared identifier 'x' (clangd)"
QString describeError(const QString &path, const QJsonObject &error)
{
    const LspProtocol::Range range = LspProtocol::rangeFrom(error.value(QLatin1String("range")));
    QString line = QStringLiteral("- %1:%2:%3: %4")
                       .arg(path)
                       .arg(range.start.line + 1)
                       .arg(range.start.character + 1)
                       .arg(messageOf(error));
    const QString source = error.value(QLatin1String("source")).toString();
    if (!source.isEmpty())
        line += QStringLiteral(" (%1)").arg(source);
    return line;
}

QString relativeTo(const QString &folder, const QString &path)
{
    if (folder.isEmpty())
        return path;
    const QString relative = QDir(folder).relativeFilePath(path);
    return relative.startsWith(QLatin1String("..")) ? path : relative;
}

// A file of a check: its errors before the edit, when they were known.
struct CheckedFile {
    QString path; // absolute
    std::optional<QJsonArray> errorsBefore;
};

// One checkAfterEdit(): waits for the servers to report the problems of the
// files they were sent, then reports. Deletes itself once it has.
class PendingCheck : public QObject {
public:
    PendingCheck(LanguageServers *servers,
                 QList<CheckedFile> files,
                 QSet<QString> waiting,
                 EditErrorCheck::Done done,
                 QObject *parent)
        : QObject(parent)
        , servers_(servers)
        , files_(std::move(files))
        , waiting_(std::move(waiting))
        , done_(std::move(done))
    {
        connect(servers_, &LanguageServers::diagnosticsChanged, this, [this](const QString &path) {
            waiting_.remove(path);
            if (waiting_.isEmpty())
                finish();
        });
        QTimer::singleShot(waiting_.isEmpty() ? 0 : diagnosticsWaitMs, this, &PendingCheck::finish);
    }

private:
    void finish()
    {
        if (finished_)
            return;
        finished_ = true;
        QList<EditErrors::FileErrors> reported;
        for (const CheckedFile &file : std::as_const(files_)) {
            if (waiting_.contains(file.path))
                continue; // what the server knows is the text before the edit
            const QJsonArray after = servers_->diagnostics(file.path);
            const QJsonArray errors =
                file.errorsBefore ? EditErrors::introduced(*file.errorsBefore, after) : EditErrors::errorsAmong(after);
            reported.append({relativeTo(servers_->folder(), file.path), errors});
        }
        done_(EditErrors::describe(reported));
        deleteLater();
    }

    LanguageServers *servers_;
    QList<CheckedFile> files_;
    QSet<QString> waiting_;
    EditErrorCheck::Done done_;
    bool finished_ = false;
};

} // namespace

namespace EditErrors {

QJsonArray errorsAmong(const QJsonArray &diagnostics)
{
    QJsonArray errors;
    for (const QJsonValue &diagnostic : diagnostics) {
        // Without a severity, an error: what the editor shows it as.
        const int severity = diagnostic.toObject().value(QLatin1String("severity")).toInt(lspErrorSeverity);
        if (severity == lspErrorSeverity)
            errors.append(diagnostic);
    }
    return errors;
}

QJsonArray introduced(const QJsonArray &before, const QJsonArray &after)
{
    QStringList known;
    for (const QJsonValue &error : errorsAmong(before))
        known.append(messageOf(error));
    QJsonArray brought;
    for (const QJsonValue &error : errorsAmong(after)) {
        // Each error before the edit accounts for one after it.
        if (!known.removeOne(messageOf(error)))
            brought.append(error);
    }
    return brought;
}

QString describe(const QList<FileErrors> &files)
{
    QStringList lines;
    for (const FileErrors &file : files) {
        for (qsizetype i = 0; i < file.errors.size() && i < maximumErrorsPerFile; ++i)
            lines.append(describeError(file.path, file.errors.at(i).toObject()));
        if (file.errors.size() > maximumErrorsPerFile)
            lines.append(
                QStringLiteral("- and %1 more in %2").arg(file.errors.size() - maximumErrorsPerFile).arg(file.path));
    }
    if (lines.isEmpty())
        return {};
    lines.prepend(QStringLiteral("Kodamiko's language server reports errors in the code you just edited:"));
    return lines.join(QLatin1Char('\n'));
}

} // namespace EditErrors

EditErrorCheck::EditErrorCheck(LanguageServers *servers, QObject *parent)
    : QObject(parent)
    , servers_(servers)
{
}

void EditErrorCheck::rememberErrorsBefore(const QStringList &paths)
{
    const QStringList known = servers_->openDocuments();
    for (const QString &path : paths) {
        const QString cleanPath = QDir::cleanPath(path);
        // A file its server never read: its errors are not known.
        if (known.contains(cleanPath))
            errorsBefore_.insert(cleanPath, EditErrors::errorsAmong(servers_->diagnostics(cleanPath)));
        else
            errorsBefore_.remove(cleanPath);
    }
}

void EditErrorCheck::checkAfterEdit(const QStringList &paths, Done done)
{
    QList<CheckedFile> files;
    QSet<QString> waiting;
    for (const QString &path : paths) {
        const QString cleanPath = QDir::cleanPath(path);
        std::optional<QJsonArray> errorsBefore;
        if (errorsBefore_.contains(cleanPath))
            errorsBefore = errorsBefore_.take(cleanPath);
        const QString languageId = LanguageRegistry::languageForFile(cleanPath);
        LspClient *client = servers_->runningClientFor(languageId);
        QString error;
        const std::optional<DecodedText> text = client ? TextFile::load(cleanPath, &error) : std::nullopt;
        if (!text)
            continue;
        // The agent never edits a file with unsaved changes (UnsavedEditGuard):
        // the file on disk is what the editor shows once it has reloaded it.
        if (servers_->syncDocument(client, cleanPath, languageId, text->text.toUtf8()))
            waiting.insert(cleanPath);
        files.append({cleanPath, std::move(errorsBefore)});
    }
    if (files.isEmpty()) {
        done({});
        return;
    }
    new PendingCheck(servers_, std::move(files), std::move(waiting), std::move(done), this);
}
