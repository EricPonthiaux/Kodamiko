// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewSession.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;
class ReviewSketch;
class StepTimeline;

// The page of a review tab (ReviewView) while the review runs, as the tab
// of a diagram being drawn: a page of code that reads itself (ReviewSketch),
// what is reviewed, the reviewer's steps on a timeline, the time spent, and
// Cancel. When the review fails, the error stays with Review Again and
// Close; the other outcomes are the tab's other page.
class ReviewProgressView : public QWidget {
    Q_OBJECT

public:
    // Follows `session`, not owned, which outlives the view.
    ReviewProgressView(ReviewSession *session, QWidget *parent);

signals:
    // Cancel, once the session is stopped, or Close.
    void closeRequested();

private:
    QLayout *createButtons();
    void onStateChanged(ReviewSession::State state);
    void showRunning();
    void showFailure();
    // The session's steps not on the timeline yet.
    void showNewSteps();
    void updateElapsed();

    ReviewSession *session_;
    QTimer *clock_;
    qsizetype shownStepCount_ = 0;

    ReviewSketch *sketch_;
    QLabel *title_;
    StepTimeline *steps_;
    QLabel *elapsedLabel_;
    QLabel *error_;
    QPushButton *cancel_;
    QPushButton *retry_;
    QPushButton *close_;
};
