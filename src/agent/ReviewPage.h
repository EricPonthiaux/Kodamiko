// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewSession.h"

#include <QPointer>
#include <QWidget>

class QLabel;
class QPushButton;
class ReviewBar;
class ReviewFindingsPanel;
struct Theme;

// The Review mode: the step between editing and the diff, when it is turned
// on. Without a review, what it is for and a button that starts one of the
// staged changes; with one, its progress (ReviewBar), then its findings in
// a grid over the rest of the page (ReviewFindingsPanel), whose rows lead
// to the diff.
class ReviewPage : public QWidget {
    Q_OBJECT

public:
    explicit ReviewPage(QWidget *parent = nullptr);

    // Follows `session`, not owned; null: none. Its owner sets another, or
    // null, before destroying it.
    void setSession(ReviewSession *session);
    // The agent that reviews, as the page names it.
    void setAgentName(const QString &name);
    void applyTheme(const Theme &theme);

signals:
    // The start button, or Review Again: a review of the staged changes as
    // they are now.
    void reviewRequested();
    // Cancel or Close: the review is over.
    void closeRequested();
    // A row of the grid activated: its place in the diff.
    void findingRequested(const ReviewReport::Finding &finding);

private:
    // Also sets invitationText_ and start_.
    QWidget *createInvitation();
    // The invitation without a review, the review otherwise; the grid once
    // there are findings.
    void showState();

    QPointer<ReviewSession> session_;
    QWidget *invitation_ = nullptr;
    QLabel *invitationText_ = nullptr;
    QPushButton *start_ = nullptr;
    ReviewBar *bar_;
    ReviewFindingsPanel *findings_;
};
