// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Hierarchy.h"
#include "Usage.h"
#include "WorkspaceEditing.h"
#include "lsp/LspProtocol.h"
#include "refactor/ChangeSignature.h"
#include "refactor/EditTargets.h"
#include "refactor/MoveClass.h"
#include "refactor/RefactoringOutcome.h"
#include "refactor/RenameAnalysis.h"
#include "refactor/RenameSymbolPopup.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

#include <functional>
#include <memory>

class CodeColorizer;
class CodeNavigator;
class EditorTabs;
class EditorWidget;
struct HierarchyKind;
class LanguageServers;
class LspClient;
class OccurrenceStore;
class ProblemStore;
class RefactoringHistory;
class QJsonArray;
class QPoint;

// What the file's language server knows about the code, in the editor:
// documentation under the mouse, the parameters of the call being typed,
// definitions, usages, rename, quick fixes, and the header of a source
// file (and back). Without a server, Go to Definition and Switch
// Header/Source fall back on the project's index and on file names. The
// problems of `problems` (the servers' and the builds') are underlined in
// the editors.
class CodeIntelligence : public QObject {
    Q_OBJECT

public:
    // What it works with; none is owned.
    struct Services {
        EditorTabs *tabs = nullptr;
        LanguageServers *servers = nullptr;
        CodeNavigator *navigator = nullptr;
        ProblemStore *problems = nullptr;
        // Where to look for a renamed name in comments and strings;
        // nullptr: only in the unsaved texts. Thread-safe.
        const OccurrenceStore *index = nullptr;
    };

    explicit CodeIntelligence(const Services &services, QObject *parent = nullptr);
    ~CodeIntelligence() override;

    // Documentation when the mouse rests on a name, parameters while a
    // call is typed.
    void attach(EditorWidget *editor);

    void goToDefinition();
    // The implementations of the interface or virtual function at the
    // cursor, its overrides...: what the server finds.
    void goToImplementation();
    // The declaration of the type of the variable or expression at the
    // cursor.
    void goToTypeDeclaration();
    // The usages of the name at the cursor, for the Usages pane
    // (usagesFound): the server's, else every occurrence of the word.
    void findUsages();
    void findUsagesOf(const UsageSearch &search);
    using UsagesDone = std::function<void(const UsageResults &results)>;
    // The usages of `search`, for `done` (Claude's find_usages), without
    // the Usages pane.
    void collectUsages(const UsageSearch &search, const UsagesDone &done);
    // The callers and callees of the function at the cursor, or the
    // supertypes and subtypes of the type there, for the Hierarchy pane
    // (hierarchyFound).
    void showCallHierarchy();
    void showTypeHierarchy();
    void rename();
    // The server's code actions at the cursor: its quick fixes, made at
    // once, and its refactorings, shown in the preview first.
    void showQuickFixes();
    // The class at the cursor (C++, Java) in a file of its own, its
    // members' definitions too, after a preview.
    void moveClassToOwnFile();
    void switchHeaderSource();
    // Documentation of the name at the cursor (Ctrl+Q).
    void showDocumentation();
    // Parameters of the call around the cursor (Ctrl+P).
    void showParameterInfo();
    // The same in `editor`, quietly: nothing said when there is no call
    // or no server (after a function was chosen in the completion list).
    void requestParameterInfo(EditorWidget *editor);

    // Applies `edit` to the open editors (one undo step each) and to the
    // files on disk, all of it or nothing (see WorkspaceEditing), and
    // records it in history() as `description`. Returns the number of
    // files changed.
    int applyWorkspaceEdit(const LspProtocol::WorkspaceEdit &edit, const QString &description);
    // `from` was renamed or moved to `to` (a file or a folder): the
    // includes and imports that pointed to it are updated, in the
    // background, as one refactoring.
    void updateReferencesToMovedFile(const QString &from, const QString &to);
    // Undoes the last refactoring in every file it changed.
    void undoRefactoring();

    // What Claude's rename_symbol asks.
    struct RenameRequest {
        QString path;
        LspProtocol::Position position; // of the name
        QString newName;
        bool inCommentsAndStrings = false;
        Review review = Review::WhenNeeded;
    };

    // Renames the symbol at `request.position`, as Rename does; `done`
    // says how it ended (after the user's choice when it is previewed).
    void renameSymbolAt(const RenameRequest &request, const RefactoringDone &done);
    // Change Signature at the cursor: its popup, then the preview.
    void changeSignature();

    // What Change Signature is asked: the function named at `position` of
    // `path` gets `parameters`.
    struct SignatureRequest {
        QString path;
        LspProtocol::Position position;
        QList<ChangeSignature::Parameter> parameters;
        Review review = Review::Always;
    };

    // Changes the signature in the function's declarations and at its
    // calls (from the server's references).
    void changeSignatureAt(const SignatureRequest &request, const RefactoringDone &done);
    // Renames or moves a file or folder with its includes and imports, as
    // one refactoring.
    void renameFile(const QString &from, const QString &to, Review review, const RefactoringDone &done);
    // `proposal` made, after the preview when `review` asks or when it has
    // textual edits or conflicts.
    void propose(const Proposal &proposal, Review review, const RefactoringDone &done);

    // The refactorings made, for the Undo Refactoring command.
    const RefactoringHistory *history() const { return history_; }

    // Byte position in `editor` of an LSP position, and back.
    static int positionOf(EditorWidget *editor, const LspProtocol::Position &position);
    static LspProtocol::Position lspPosition(EditorWidget *editor, int bytePosition);
    // Header of a source file or source of a header, next to it or in a
    // sibling include/src folder; empty if there is none. Public for tests.
    static QString counterpartOf(const QString &filePath);

signals:
    void statusMessage(const QString &message);
    void usagesFound(const UsageResults &results);
    void hierarchyFound(const Hierarchy &hierarchy);

private:
    // The server of `editor`'s language, with the editor's text sent to it;
    // nullptr (with a message, unless `quiet`) when there is none.
    LspClient *serverFor(EditorWidget *editor, bool quiet = false);

    EditTargets targets() const { return {.tabs = tabs_, .servers = servers_}; }

    // applyWorkspaceEdit(), with its errors.
    WorkspaceEditing::Outcome applyEdit(const LspProtocol::WorkspaceEdit &edit, const QString &description);
    QJsonObject positionParams(EditorWidget *editor, int bytePosition) const;
    // Goes to the `method` locations at the cursor, if the server has the
    // `capability` ("implementationProvider"); says `noneFound` if none.
    void goToServerLocations(const QString &method, const QString &capability, const QString &noneFound);
    // Asks `client` for the `method` locations at the cursor of `editor`,
    // and goes to the one found, or to the one chosen in a menu; calls
    // `noneFound` (with the server's error, if any) when there is none.
    void goToLocations(EditorWidget *editor,
                       LspClient *client,
                       const QString &method,
                       const std::function<void(const QString &error)> &noneFound);
    // Sends the text of the other open files of `client`'s languages: a
    // rename or a search for usages covers them, unsaved changes included,
    // even before the server has indexed the project.
    void syncOpenEditors(LspClient *client, const QString &languageId);

    // A rename asked in the popup.
    // A rename asked in the popup, or by Claude.
    struct Rename {
        QString path;
        LspProtocol::Position position; // of the name
        QString name;
        RenameSymbolPopup::Choice choice;
        RefactoringDone done; // may be empty
    };

    // Asks for the new name of `name`, at `bytePosition` in `editor`.
    void showRenamePopup(EditorWidget *editor, int bytePosition, const QString &name);
    // Asks the server for the edit of `rename`, then makes it, or shows it
    // first.
    void requestRename(const Rename &rename);
    // Looks for the name in comments and strings and for conflicts, in the
    // background, then completes the rename.
    void analyzeRename(const LspProtocol::WorkspaceEdit &edit, const Rename &rename);
    void completeAnalyzedRename(const LspProtocol::WorkspaceEdit &edit,
                                const RenameAnalysis::Result &analysis,
                                const Rename &rename);
    // The servers still reading the project, as a conflict: they may not
    // know every usage yet.
    QStringList busyServers() const;
    // Makes `edit` as one refactoring named `title`.
    void complete(const QString &title, const LspProtocol::WorkspaceEdit &edit, const RefactoringDone &done);
    // `proposal` in the preview popup, made once confirmed: what the user
    // keeps, as one refactoring; cancelled if the popup closes otherwise.
    void review(const Proposal &proposal, const RefactoringDone &done);
    // Calls `done`, if any, with `outcome`.
    static void finish(const RefactoringDone &done, const RefactoringOutcome &outcome);
    // The identifier at `position` of `path` (its editor's text if open).
    QString wordAt(const QString &path, const LspProtocol::Position &position) const;
    // `path`'s text: its editor's if open, else the file's.
    QString currentText(const QString &path) const;

    // A signature change, with the places the server found.
    struct SignatureChange {
        ChangeSignature::Signature signature;
        QList<ChangeSignature::Parameter> parameters;
        QList<LspProtocol::Location> places;
        Review review = Review::Always;
        RefactoringDone done;
    };

    // Works the edit of `change` out off the UI thread, then proposes it.
    void planSignatureChange(const SignatureChange &change);
    // What Move Class needs from `editor`, and from its source.
    MoveClass::Input moveClassInput(EditorWidget *editor) const;
    void completeReferenceUpdate(const LspProtocol::WorkspaceEdit &edit, const QString &name);
    void requestSignatureHelp(EditorWidget *editor, bool explicitRequest);
    // Shows the server's signature help `help` as a call tip.
    void showSignatureHelp(const EditorWidget *editor, const QJsonObject &help, bool explicitRequest);
    // The server of `path`'s language, with the open files sent to it;
    // nullptr when there is none.
    LspClient *usageServerFor(const QString &path);
    // The kinds of the server's `locations`, for `done`, off the
    // UI thread.
    void classifyUsages(const UsageSearch &search,
                        const QList<LspProtocol::Location> &locations,
                        const UsagesDone &done);
    // Every occurrence of the word, from the index, for `done`.
    void findWordUsages(const UsageSearch &search, const UsagesDone &done);
    void reportUsages(const UsageResults &results);
    // `hierarchy`'s roots asked to the server, `title` completed with the
    // name at the cursor.
    void showHierarchy(const HierarchyKind &kind, Hierarchy hierarchy);
    // Offers the server's `codeActions` in a menu, and carries out the
    // chosen one.
    void chooseQuickFix(EditorWidget *editor, LspClient *server, const QJsonArray &codeActions);
    // A server's code action: its edit (after a preview for a
    // refactoring), then its command.
    void applyCodeAction(LspClient *server, const QJsonObject &action);
    // The servers read the files no longer open from disk again.
    void closeDocumentsOfClosedTabs();
    // Shows the problems of `path` in its editor.
    void showProblems(const QString &path);
    void showHover(EditorWidget *editor, int bytePosition, const QPoint &where, bool explicitRequest);
    // The occurrences of the symbol at `bytePosition`, from a server that
    // already runs, highlighted in place of the text's whole words.
    void requestOccurrences(EditorWidget *editor, int bytePosition);
    // Below the cursor of `editor`, in global coordinates.
    static QPoint belowCaret(const EditorWidget *editor);
    // Menu under the cursor of `editor`; returns the chosen entry or -1.
    int chooseFrom(EditorWidget *editor, const QStringList &entries) const;
    // Documentation in `markdown` as a tooltip, in the theme of `editor`.
    QString documentationHtml(const QString &markdown, const EditorWidget &editor);
    void jumpTo(const LspProtocol::Location &location);
    QString describe(const LspProtocol::Location &location, QHash<QString, QStringList> &lines) const;

    EditorTabs *tabs_;
    LanguageServers *servers_;
    CodeNavigator *navigator_;
    ProblemStore *problems_;
    const OccurrenceStore *index_;
    RefactoringHistory *history_; // owned, as a child
    // Highlights the code of the documentation; built on first use (it
    // holds a hidden editor).
    std::unique_ptr<CodeColorizer> colorizer_;
    int hoverGeneration_ = 0; // answers for an older position are dropped
    int signatureGeneration_ = 0;
};
