// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewFindingsList.h"

#include <QPointer>
#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;

// A review's findings (ReviewSession) to go through and act on: a bar with
// how many findings, errors and warnings are left, which also filter the
// list, then Resolve, Send to <agent> and Dismiss for the selected findings,
// or Resolve All and Send All for the findings shown when none is selected;
// under it, the findings grouped by file (ReviewFindingsList). In a review's
// tab (ReviewView) and over the diff (ReviewFindingsPanel) alike.
class ReviewFindingsView : public QWidget {
    Q_OBJECT

public:
    explicit ReviewFindingsView(QWidget *parent = nullptr);

    // Follows `session`, not owned; null: none.
    void setSession(ReviewSession *session);

signals:
    // A finding activated in the list: its place.
    void findingRequested(const ReviewReport::Finding &finding);

private:
    QLayout *createBar();
    QToolButton *createFilter(ReviewFindingsList::Filter filter, const QString &dotColor);
    void updateBar();
    // The selected findings, or all those shown when none is.
    QList<ReviewReport::Finding> targets() const;

    QPointer<ReviewSession> session_;
    ReviewFindingsList *list_;
    QToolButton *all_ = nullptr;
    QToolButton *errors_ = nullptr;
    QToolButton *warnings_ = nullptr;
    QLabel *selection_ = nullptr;
    QPushButton *resolve_ = nullptr;
    QPushButton *send_ = nullptr;
    QPushButton *dismiss_ = nullptr;
};
