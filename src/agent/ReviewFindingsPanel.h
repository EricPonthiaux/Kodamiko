// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewSession.h"

#include <QFrame>
#include <QPointer>

class QLabel;
class QToolButton;
class ReviewFindingsView;
struct Theme;

// A review's findings (ReviewSession) over the diff they are about
// (ReviewFindingsView), under the reviewer's summary; its close button
// gives the diff back. Its owner lays it over the diff, at its size.
class ReviewFindingsPanel : public QFrame {
    Q_OBJECT

public:
    explicit ReviewFindingsPanel(QWidget *parent = nullptr);

    // Follows `session`, not owned; null: none.
    void setSession(ReviewSession *session);
    // With its close button (the default), or without.
    void setClosable(bool closable);
    void applyTheme(const Theme &theme);

signals:
    // A finding activated: its place in the diff.
    void findingRequested(const ReviewReport::Finding &finding);
    void closeRequested();

private:
    void showSummary();

    QPointer<ReviewSession> session_;
    QLabel *title_;
    QLabel *summary_;
    QToolButton *close_;
    ReviewFindingsView *findings_;
};
