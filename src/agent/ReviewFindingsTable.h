// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewSession.h"

#include <QPointer>
#include <QTableWidget>

// The findings of a review (ReviewSession) in a grid, one row each, to see
// them all at a glance: severity, place, problem and scenario, the text
// wrapped to the columns' width, and Resolve at the end, which hands the
// finding to the session that wrote the code to fix. Double-click or Enter
// on a row asks for its place (findingRequested()); Delete dismisses the
// selected rows, and the menu resolves, sends or dismisses them.
class ReviewFindingsTable : public QTableWidget {
    Q_OBJECT

public:
    explicit ReviewFindingsTable(QWidget *parent = nullptr);

    // Follows `session`, not owned; null: none, the grid empty.
    void setSession(ReviewSession *session);

signals:
    // A row activated.
    void findingRequested(const ReviewReport::Finding &finding);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    enum class Column {
        Severity,
        Place,
        Problem,
        Scenario,
        Resolve
    };

    void setUpColumns();
    // The rows' heights for the columns' widths once they are laid out:
    // the stretched columns take theirs after the event.
    void fitRowsLater();
    // A row per finding left.
    void fill();
    void addRow(const ReviewReport::Finding &finding);
    // The Resolve button of `finding`'s row, in a cell of its own.
    QWidget *createResolveCell(const ReviewReport::Finding &finding);
    QList<ReviewReport::Finding> selectedFindings() const;
    void showMenu(const QPoint &position);

    QPointer<ReviewSession> session_;
    QList<ReviewReport::Finding> rows_; // the finding of each row
};
