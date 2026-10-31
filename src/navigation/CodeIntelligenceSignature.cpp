// SPDX-License-Identifier: GPL-3.0-only

// CodeIntelligence: Change Signature, Rename File, and the refactorings
// Claude proposes.

#include "CodeIntelligence.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/TextFile.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspClient.h"
#include "refactor/ChangeSignaturePopup.h"
#include "refactor/EditPreview.h"
#include "refactor/FileReferences.h"
#include "syntax/LanguageRegistry.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QThreadPool>

using namespace LspProtocol;

namespace {

// The texts of the files of `places`: an open file's editor text, else the
// file's; read off the UI thread.
QHash<QString, QString> textsOf(const QList<Location> &places, const QHash<QString, QString> &unsavedTexts)
{
    QHash<QString, QString> texts;
    for (const Location &place : places) {
        if (texts.contains(place.filePath))
            continue;
        if (unsavedTexts.contains(place.filePath)) {
            texts.insert(place.filePath, unsavedTexts.value(place.filePath));
            continue;
        }
        QString error;
        const std::optional<DecodedText> decoded = TextFile::load(place.filePath, &error);
        texts.insert(place.filePath, decoded ? decoded->text : QString());
    }
    return texts;
}

bool containsPlace(const QList<Location> &places, const QString &path, const Position &position)
{
    return std::ranges::any_of(places, [&](const Location &place) {
        return place.filePath == path && place.range.start.line == position.line;
    });
}

} // namespace

void CodeIntelligence::changeSignature()
{
    EditorWidget *editor = tabs_->currentEditor();
    if (!editor || editor->filePath().isEmpty() || editor->isReadOnly())
        return;
    const Position position = lspPosition(editor, int(editor->send(SCI_GETCURRENTPOS)));
    const QString languageId = LanguageRegistry::languageForFile(editor->filePath());
    const ChangeSignature::Signature signature = ChangeSignature::read(languageId, editor->text(), position);
    if (!signature.error.isEmpty()) {
        emit statusMessage(signature.error);
        return;
    }
    auto *popup = new ChangeSignaturePopup(
        languageId, signature,
        [this, path = editor->filePath(), position](const QList<ChangeSignature::Parameter> &parameters) {
            changeSignatureAt({path, position, parameters, Review::Always}, {});
        },
        tabs_->window());
    popup->popup();
}

void CodeIntelligence::changeSignatureAt(const SignatureRequest &request, const RefactoringDone &done)
{
    const QString &path = request.path;
    const Position &position = request.position;
    const QString languageId = LanguageRegistry::languageForFile(path);
    const ChangeSignature::Signature signature = ChangeSignature::read(languageId, currentText(path), position);
    LspClient *client = signature.error.isEmpty() ? usageServerFor(path) : nullptr;
    if (!client) {
        const QString message = signature.error.isEmpty()
                                    ? tr("No language server to find the calls of “%1”").arg(signature.name)
                                    : signature.error;
        emit statusMessage(message);
        finish(done, {.messages = {message}});
        return;
    }
    const QJsonObject params{
        {"textDocument", QJsonObject{{"uri", uriOf(path)}}},
        {"position", toJson(position)},
        {"context", QJsonObject{{"includeDeclaration", true}}},
    };
    emit statusMessage(tr("Looking for the calls of “%1”…").arg(signature.name));
    client->request(QStringLiteral("textDocument/references"), params,
                    [this, request, signature, done](const QJsonValue &result, const QString &error) {
                        if (!error.isEmpty()) {
                            emit statusMessage(tr("Change Signature failed: %1").arg(error));
                            finish(done, {.messages = {error}});
                            return;
                        }
                        QList<Location> places = parseLocations(result);
                        if (!containsPlace(places, request.path, request.position))
                            places.append({request.path, {request.position, request.position}});
                        planSignatureChange({signature, request.parameters, places, request.review, done});
                    });
}

void CodeIntelligence::planSignatureChange(const SignatureChange &change)
{
    QThreadPool::globalInstance()->start(
        [guard = QPointer<CodeIntelligence>(this), change, unsaved = tabs_->unsavedTexts()] {
            const ChangeSignature::Input input{change.parameters, change.signature.names, change.places,
                                               textsOf(change.places, unsaved)};
            ChangeSignature::Result result = ChangeSignature::plan(input);
            QMetaObject::invokeMethod(
                qApp,
                [guard, change, result = std::move(result)] {
                    if (!guard)
                        return;
                    const QString title = tr("Change the signature of “%1”").arg(change.signature.name);
                    guard->propose({title, result.edit, {}, result.conflicts}, change.review, change.done);
                },
                Qt::QueuedConnection);
        });
}

void CodeIntelligence::propose(const Proposal &proposal, Review review, const RefactoringDone &done)
{
    if (proposal.edit.isEmpty() && proposal.textualEdit.isEmpty()) {
        finish(done,
               {.messages = proposal.conflicts.isEmpty() ? QStringList{tr("Nothing to change")} : proposal.conflicts});
        return;
    }
    const bool needsReview =
        review == Review::Always || !proposal.textualEdit.isEmpty() || !proposal.conflicts.isEmpty();
    if (needsReview)
        this->review(proposal, done);
    else
        complete(proposal.title, EditPreview::merge(proposal.edit, proposal.textualEdit), done);
}

void CodeIntelligence::renameFile(const QString &from, const QString &to, Review review, const RefactoringDone &done)
{
    QString problem;
    if (!QFileInfo::exists(from))
        problem = tr("%1 does not exist").arg(from);
    else if (QFileInfo::exists(to))
        problem = tr("%1 already exists").arg(to);
    if (!problem.isEmpty()) {
        finish(done, {.messages = {problem}});
        return;
    }
    QList<FileReferences::Move> moves;
    if (QFileInfo(from).isDir())
        moves = FileReferences::filesOf(from, to);
    else
        moves.append({from, to});
    FileReferences::Input input{moves, servers_->folder(), index_, tabs_->unsavedTexts()};
    const bool isRename = QFileInfo(from).absolutePath() == QFileInfo(to).absolutePath();
    const QString title = isRename ? tr("Rename “%1” to “%2”").arg(QFileInfo(from).fileName(), QFileInfo(to).fileName())
                                   : tr("Move “%1” to %2")
                                         .arg(QFileInfo(from).fileName(),
                                              QDir::toNativeSeparators(QDir(servers_->folder()).relativeFilePath(to)));
    QThreadPool::globalInstance()->start(
        [guard = QPointer<CodeIntelligence>(this), input = std::move(input), from, to, title, review, done] {
            const WorkspaceEdit references = FileReferences::update(input);
            QMetaObject::invokeMethod(
                qApp,
                [guard, references, from, to, title, review, done] {
                    if (!guard)
                        return;
                    // One refactoring: the move, then the references.
                    WorkspaceEdit edit{
                        {FileOperation{.kind = FileOperation::Kind::Rename, .filePath = from, .newPath = to}}};
                    edit.changes.append(references.changes);
                    guard->propose({title, edit, {}, {}}, review, done);
                },
                Qt::QueuedConnection);
        });
}
