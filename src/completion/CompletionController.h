#pragma once

#include "Completion.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

class CompletionPopup;
class CompletionService;
class EditorWidget;

// Completion in one editor: opens the popup on Ctrl+Space, while typing an
// identifier (when EditorOptions::autoCompletion is on) and after a trigger
// character ("."), filters it as the user types, and inserts the chosen
// candidate. While the popup is open, Up/Down/Page Up/Page Down move in
// it, Enter inserts, Tab replaces the whole identifier, Escape closes.
class CompletionController : public QObject {
    Q_OBJECT

public:
    CompletionController(EditorWidget *editor, CompletionService *service);
    ~CompletionController() override;

    // The controller of `editor`, nullptr if it has none.
    static CompletionController *of(const EditorWidget *editor);

    // Ctrl+Space: opens the popup even with nothing typed.
    void complete();
    bool isActive() const;
    CompletionPopup *popup() const;
    void close();
    // Inserts the selected candidate; `replaceWord` also replaces the rest
    // of the identifier after the cursor (Tab).
    void accept(bool replaceWord = false);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void start(bool explicitRequest, QChar triggerCharacter);
    void onCharacterAdded(int character);
    void refilter();
    void showPopup();
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
    QTimer autoTimer_;

    // The session: from opening to closing the popup.
    bool active_ = false;
    int prefixStart_ = 0;
    int line_ = 0;
    QList<CompletionItem> items_;
    // A provider sent part of its candidates for `requestedPrefix_`: a
    // longer prefix asks again.
    bool incomplete_ = false;
    QString requestedPrefix_;
};
