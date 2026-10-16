#pragma once

#include <QObject>

class QTimer;
class QTreeView;

// Single-column tree whose names are never truncated: the column takes
// the width of the longest visible name (at least the view's), and a
// horizontal scroll bar appears at the bottom when it overflows.
//
// The fit follows the tree's model, expansion, size and font; a change it
// cannot see (hidden rows, blocked signals) requires calling
// refit().
class TreeColumnFit : public QObject {
    Q_OBJECT

public:
    explicit TreeColumnFit(QTreeView *tree);

    void refit();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void fit();

    QTreeView *tree_;
    QTimer *timer_; // coalesces requests from the same pass
};
