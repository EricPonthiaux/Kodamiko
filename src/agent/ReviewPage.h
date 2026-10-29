// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewSession.h"

#include <QPointer>
#include <QWidget>

class QLabel;
class QPushButton;
class ReviewView;
struct Theme;

// The Review mode: the step between editing and the diff, when it is turned
// on. An invitation starts a review of staged changes; ReviewView presents
// the run and its outcomes, and findings lead to their place in the diff.
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
    // Close: discard this review and return to the invitation.
    void closeRequested();
    void inspectRequested();
    // A finding activated (a double click, Enter, or a click on its line):
    // its place in the diff.
    void findingRequested(const ReviewReport::Finding &finding);

private:
    // Also sets invitationText_ and start_.
    QWidget *createInvitation();
    QPointer<ReviewSession> session_;
    QWidget *invitation_ = nullptr;
    QLabel *invitationText_ = nullptr;
    QPushButton *start_ = nullptr;
    ReviewView *view_ = nullptr;
};
