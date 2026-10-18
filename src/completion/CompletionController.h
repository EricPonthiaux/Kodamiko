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
// Ctrl+Space again shows more candidates (extended completion), Ctrl+Q
// shows or hides the documentation, Escape closes. Ctrl+Shift+Space only
// offers what has the expected type (smart completion).
//
// A candidate offering choices (the expression a postfix template wraps
// in "a + b.if") lists them once chosen, the expression of the selected
// one highlighted in the code.
//
// In a documentation comment, "@" (and "\" in C and C++) opens its tags,
// "@param " the parameters; Enter after "/**" writes the comment of the
// function below.
//
// With a Full Line model (see CompletionService::fullLineModel), a pause
// at the end of a line shows its suggested rest in gray: Tab inserts it,
// Ctrl+Right its next word, Escape dismisses it, typing what it shows
// keeps it.
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
    // Ctrl+Shift+Space: only the candidates of the expected type.
    void completeSmart();
    // Shows or hides the documentation beside the list (Ctrl+Q).
    void toggleDocumentation();
    bool isActive() const;

    // The kind of completion shown.
    CompletionContext::Mode mode() const { return mode_; }

    // Whether the list shows the expressions to choose from.
    bool isChoosing() const { return choosing_; }

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

    // The Full Line suggestion shown, empty when none. For tests.
    QString fullLineText() const { return fullLine_; }

    // The placeholders of the snippet last inserted.
    SnippetSession *snippet() const { return snippet_; }

signals:
    // A function's call was inserted, the cursor in its parentheses: its
    // parameters can be shown.
    void callInserted();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    using Mode = CompletionContext::Mode;

    void start(bool explicitRequest, QChar triggerCharacter, Mode mode = Mode::Basic);
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
    bool insideDocComment(int position) const;
    // In the module name of a JavaScript import: "from './|'".
    bool inModulePath(int position) const;
    // What opens completion in a documentation comment: "@", "\\", the
    // space after "@param". Null when nothing.
    QChar docCommentTrigger(QChar typed, int position) const;
    // Enter after "/**": the comment of the declaration below. Returns
    // whether it was written.
    bool writeDocComment(int position);
    // Replaces [start, end) with `snippet` (one undo step), selects its
    // first stop and follows its stops.
    void insertSnippet(int start, int end, const QString &snippet);
    // Lists the item's choices instead of the candidates.
    void beginChoice(const CompletionItem &item);
    void highlightChoice();
    void updateKeyHint();
    // Full Line completion.
    void followFullLine(QChar typed, int position);
    void scheduleFullLine(int position);
    void requestFullLine();
    void showFullLine();
    void clearFullLine();
    void acceptFullLine(bool nextWordOnly);
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
    QTimer fullLineTimer_;   // asks the model once typing pauses
    QChar pendingTrigger_;   // what autoTimer_ opens completion after
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
    Mode mode_ = Mode::Basic;
    QChar trigger_;        // what opened the session
    QString expectedType_; // smart completion's
    // The item whose choices the list shows.
    bool choosing_ = false;
    CompletionItem choosingItem_;

    // The Full Line suggestion shown after the cursor at fullLinePosition_.
    QString fullLine_;
    int fullLinePosition_ = -1;
    int fullLineGeneration_ = 0; // answers to an older request are dropped
};
