#pragma once

#include "Completion.h"
#include "ui/Theme.h"

#include <QFrame>
#include <QTimer>

class QLabel;
class QListView;
class CompletionListModel;

// List of candidates under the cursor: a badge for the kind ("m" for a
// method, "c" for a class...), the label with the typed letters
// highlighted, the detail at the right, and a line saying which keys do
// what. Beside it, the selected candidate's kind, signature and
// documentation. It never takes the focus: the editor keeps the keyboard
// and forwards the navigation keys.
//
// For screen readers, each row reads as "getValue, method, int getValue()",
// and opening the list or moving in it is announced (see announce()).
class CompletionPopup : public QFrame {
    Q_OBJECT

public:
    explicit CompletionPopup(QWidget *parent);
    ~CompletionPopup() override;

    // Filters and ranks `items` by `prefix`; returns how many match.
    int setItems(const QList<CompletionItem> &items, const QString &prefix);
    int count() const;
    // The selected candidate; nullptr when the list is empty.
    const CompletionItem *current() const;
    int currentRow() const;
    void setCurrentRow(int row);
    // Moves the selection by `rows` (negative: up), stopping at the ends.
    void moveSelection(int rows);
    // Rows of a page, for Page Up / Page Down.
    int pageRows() const;
    void applyTheme(const Theme &theme);
    // Labels in display order, for tests.
    QStringList labels() const;

    // The keys that insert, replace and close, shown under the list.
    void setKeyHint(const QString &hint);
    QString keyHint() const;
    // The documentation beside the list (Ctrl+Space shows or hides it; the
    // choice is remembered).
    void setDocumentationVisible(bool visible);
    bool isDocumentationVisible() const { return documentationVisible_; }
    // Its text, for tests: empty when it is hidden.
    QString documentationText() const;

    // "getValue, method, int getValue(), 3 of 12".
    QString describeCurrent() const;
    // Says `message` through the screen reader, when one is running.
    static void announce(const QString &message);

    // Shows the popup with its top left corner at `anchor` (in the parent's
    // coordinates), above `anchorTop` when there is no room below.
    void showAt(const QPoint &anchor, int anchorTop);

signals:
    // A candidate was double-clicked.
    void activated();
    // Another candidate is selected.
    void currentChanged();

protected:
    void hideEvent(QHideEvent *event) override;

private:
    void fitSize();
    void updateDocumentation();
    void resolveDocumentation();
    void placeDocumentation();

    QListView *view_;
    CompletionListModel *model_;
    QLabel *hint_;
    // Beside the list, in the same parent.
    QFrame *documentation_;
    QLabel *documentationTitle_;
    QLabel *documentationBody_;
    bool documentationVisible_ = true;
    QTimer resolveTimer_; // asks a server for the documentation once the selection rests
    Theme theme_;
};
