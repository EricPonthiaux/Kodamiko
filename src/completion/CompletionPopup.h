#pragma once

#include "Completion.h"
#include "ui/Theme.h"

#include <QFrame>

class QListView;
class CompletionListModel;

// List of candidates under the cursor: a badge for the kind ("m" for a
// method, "c" for a class...), the label with the typed letters
// highlighted, the detail at the right. It never takes the focus: the
// editor keeps the keyboard and forwards the navigation keys.
class CompletionPopup : public QFrame {
    Q_OBJECT

public:
    explicit CompletionPopup(QWidget *parent);

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

    // Shows the popup with its top left corner at `anchor` (in the parent's
    // coordinates), above `anchorTop` when there is no room below.
    void showAt(const QPoint &anchor, int anchorTop);

signals:
    // A candidate was double-clicked.
    void activated();

private:
    void fitSize();

    QListView *view_;
    CompletionListModel *model_;
    Theme theme_;
};
