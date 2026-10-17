#pragma once

#include "Completion.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <optional>

class CompletionPopup;
class CompletionService;
class EditorWidget;
class SnippetSession;

// Completion in one editor: opens the popup on Ctrl+Space, and while typing
// an identifier or after a trigger character (".") as
// EditorOptions::autoCompletion says, filters it as the user types, and
// inserts the chosen candidate. While the popup is open, Up/Down/Page
// Up/Page Down move in it, Enter inserts (unless
// EditorOptions::completionAcceptsEnter is off), Tab replaces the whole
// identifier, Ctrl+Right inserts the next word of the candidate,
// Ctrl+Space shows or hides its documentation, Escape closes.
//
// A candidate that is a snippet (a call's arguments, a template) leaves
// its placeholders to fill in: Tab goes to the next one (see
// SnippetSession).
//
// The selected candidate's rest shows in gray after the cursor, at the end
// of a line. The candidates the user chose before rank higher, those
// erased right after being inserted lower (see CompletionHistory).
class CompletionController : public QObject {
    Q_OBJECT

public:
    CompletionController(EditorWidget *editor, CompletionService *service);
    ~CompletionController() override;

    // The controller of `editor`, nullptr if it has none.
    static CompletionController *of(const EditorWidget *editor);

    // Ctrl+Space: opens the popup even with nothing typed; when it is
    // open, shows or hides the documentation.
    void complete();
    bool isActive() const;
    CompletionPopup *popup() const;
    void close();
    // Inserts the selected candidate; `replaceWord` also replaces the rest
    // of the identifier after the cursor (Tab).
    void accept(bool replaceWord = false);
    // Inserts the selected candidate up to the end of its next word
    // ("getSelected" of "getSelectedValue" after "get").
    void acceptNextWord();
    // The gray rest of the selected candidate, empty when hidden. For tests.
    QString previewText() const;
    // The placeholders of the snippet last inserted.
    SnippetSession *snippet() const { return snippet_; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void start(bool explicitRequest, QChar triggerCharacter);
    void onCharacterAdded(int character);
    void refilter();
    void showPopup();
    // Ranks the candidates by the user's past choices.
    void applyHistory(QList<CompletionItem> &items) const;
    void onCurrentChanged();
    void updatePreview();
    void onModified(int position, int modificationType, int length);
    // Start of the identifier ending at `position`, in bytes.
    int identifierStart(int position) const;
    bool insideCommentOrString(int position) const;
    QString textBetween(int start, int end) const;
    // Applies `edits` (computed before the insertion of `insertedBytes`
    // bytes at `insertStart`, replacing `replacedBytes`).
    void applyEdits(const QList<TextEdit> &edits, int insertStart, int insertedBytes, int replacedBytes);
    // Byte position of a protocol position in the current text.
    int positionOf(int line, int column) const;

    EditorWidget *editor_;
    CompletionService *service_;
    QPointer<CompletionPopup> popup_;
    QWidget *preview_ = nullptr; // over the editor's viewport
    SnippetSession *snippet_;
    QTimer autoTimer_;
    QChar pendingTrigger_; // what autoTimer_ opens completion after
    QString announcedLabel_; // the candidate last read to a screen reader

    // A candidate just inserted: erased within a few seconds, it counts
    // as a rejection.
    struct Inserted {
        QString label;
        int start = 0;
        int length = 0; // bytes of the label
        QElapsedTimer age;
    };
    std::optional<Inserted> inserted_;

    // The session: from opening to closing the popup.
    bool active_ = false;
    int prefixStart_ = 0;
    int line_ = 0;
    QList<CompletionItem> items_;
    // A provider sent part of its candidates for `requestedPrefix_`: a
    // longer prefix asks again.
    bool incomplete_ = false;
    QString requestedPrefix_;
    // The popup shows items_ filtered with listedPrefix_, unless stale.
    bool listStale_ = true;
    QString listedPrefix_;
};
