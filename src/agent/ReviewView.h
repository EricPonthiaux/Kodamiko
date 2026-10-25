// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewSession.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QScrollArea;
class QTextBrowser;
class QTimer;
class QVBoxLayout;
class Spinner;

// The tab of a review (ReviewSession): the project's check first, when it
// has one, then what the reviewer reads as it goes, then its findings, each
// a card with Send to <agent> and Dismiss. A failing check shows its output,
// with Review Anyway.
class ReviewView : public QWidget {
    Q_OBJECT

public:
    // The tab of a new session of `review`, which it owns.
    explicit ReviewView(ReviewSession::Request review, QWidget *parent = nullptr);

    // Owned by the view; to connect to sendRequested().
    ReviewSession *session() const { return session_; }

signals:
    // A click on a finding's place; `line` from 1.
    void openRequested(const QString &path, int line);
    // Cancel or Close.
    void closeRequested();

private:
    QLayout *createHeader();
    QWidget *createProgress();
    QWidget *createFindingsArea();
    void onStateChanged(ReviewSession::State state);
    // Back to the progress, nothing else shown.
    void showProgress();
    void showCheckFailure();
    void showFinished();
    void showFailure();
    void showSteps();
    // The cards of the session's findings.
    void showFindings();
    void updateElapsed();

    ReviewSession *session_;
    QTimer *clock_;

    QLabel *title_;
    QLabel *elapsedLabel_;
    QWidget *progress_ = nullptr; // the spinner and the steps, while the run goes on
    Spinner *spinner_ = nullptr;
    QLabel *stepsLabel_ = nullptr;
    QLabel *summary_;
    QScrollArea *findingsArea_ = nullptr;
    QVBoxLayout *findingsLayout_ = nullptr;
    QTextBrowser *rawAnswerView_; // also the output of a failing check
    QLabel *error_;
    QPushButton *cancel_;
    QPushButton *reviewAnyway_;
    QPushButton *retry_;
    QPushButton *sendAll_;
    QPushButton *close_;
};
