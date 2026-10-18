#pragma once

#include "lsp/LspProtocol.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

class CodeNavigator;
class EditorTabs;
class EditorWidget;
class LanguageServers;
class LspClient;
class ProblemStore;
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
    CodeIntelligence(EditorTabs *tabs, LanguageServers *servers, CodeNavigator *navigator, ProblemStore *problems,
                     QObject *parent = nullptr);

    // Documentation when the mouse rests on a name, parameters while a
    // call is typed.
    void attach(EditorWidget *editor);

    void goToDefinition();
    void findUsages();
    void rename();
    void showQuickFixes();
    void switchHeaderSource();
    // Documentation of the name at the cursor (Ctrl+Q).
    void showDocumentation();
    // Parameters of the call around the cursor (Ctrl+P).
    void showParameterInfo();
    // The same in `editor`, quietly: nothing said when there is no call
    // or no server (after a function was chosen in the completion list).
    void requestParameterInfo(EditorWidget *editor);

    // Applies `edit` to the open editors (one undo step each) and to the
    // files on disk. Returns the number of files changed.
    int applyWorkspaceEdit(const LspProtocol::WorkspaceEdit &edit);

    // Byte position in `editor` of an LSP position, and back.
    static int positionOf(EditorWidget *editor, const LspProtocol::Position &position);
    static LspProtocol::Position lspPosition(EditorWidget *editor, int bytePosition);
    // Header of a source file or source of a header, next to it or in a
    // sibling include/src folder; empty if there is none. Public for tests.
    static QString counterpartOf(const QString &filePath);

signals:
    void statusMessage(const QString &message);

private:
    // The server of `editor`'s language, with the editor's text sent to it;
    // nullptr (with a message, unless `quiet`) when there is none.
    LspClient *serverFor(EditorWidget *editor, bool quiet = false);
    QJsonObject positionParams(EditorWidget *editor, int bytePosition) const;
    // Sends the text of the other open files of `client`'s languages: a
    // rename or a search for usages covers them, unsaved changes included,
    // even before the server has indexed the project.
    void syncOpenEditors(LspClient *client, const QString &languageId);
    void requestSignatureHelp(EditorWidget *editor, bool explicitRequest);
    // Shows the problems of `path` in its editor.
    void showProblems(const QString &path);
    void showHover(EditorWidget *editor, int bytePosition, const QPoint &where, bool explicitRequest);
    // Menu under the cursor of `editor`; returns the chosen entry or -1.
    int chooseFrom(EditorWidget *editor, const QStringList &entries) const;
    void jumpTo(const LspProtocol::Location &location);
    QString describe(const LspProtocol::Location &location, QHash<QString, QStringList> &lines) const;

    EditorTabs *tabs_;
    LanguageServers *servers_;
    CodeNavigator *navigator_;
    ProblemStore *problems_;
    int hoverGeneration_ = 0; // answers for an older position are dropped
    int signatureGeneration_ = 0;
};
