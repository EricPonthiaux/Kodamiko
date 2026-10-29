// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewSession.h"

#include <QWidget>

class QLabel;
class QBoxLayout;
class QPushButton;
class QTimer;
class ReviewSketch;
class StepTimeline;

// The page of a review tab (ReviewView) while the review runs, as the tab
// of a diagram being drawn: a page of code that reads itself (ReviewSketch),
// what is reviewed, the reviewer's steps on a timeline, the time spent, and
// Stop review. Failures and interruptions stay visible with Review Again
// and Close; the other outcomes are the tab's other pages.
class ReviewProgressView : public QWidget {
    Q_OBJECT

public:
    // Follows `session`, not owned, which outlives the view.
    ReviewProgressView(ReviewSession *session, QWidget *parent);

signals:
    // Close the review; stopping it instead leaves its outcome visible.
    void closeRequested();
    void reviewRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QLayout *createButtons();
    void onStateChanged(ReviewSession::State state);
    void showRunning();
    void showFailure();
    void showInterrupted();
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
    QLabel *kind_ = nullptr;
    QWidget *column_ = nullptr;
    QWidget *reading_ = nullptr;
    QBoxLayout *readingLayout_ = nullptr;
    QPushButton *cancel_;
    QPushButton *retry_;
    QPushButton *close_;
};
