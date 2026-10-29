// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewSession.h"

#include <QWidget>

class QLabel;
class QHBoxLayout;
class QVBoxLayout;
class QPushButton;
class QStackedLayout;
class QTextBrowser;
class ReviewFindingsView;
class ReviewProgressView;

// The tab of a review (ReviewSession): while the check and the reviewer run,
// or when they fail, the progress page (ReviewProgressView); then the
// findings to go through (ReviewFindingsView). A failing check shows its
// output, with Review Anyway.
class ReviewView : public QWidget {
    Q_OBJECT

public:
    // The tab of a new session of `review`, which it owns.
    explicit ReviewView(ReviewSession::Request review, QWidget *parent = nullptr);
    // Follows an existing session, which outlives the view. Its owner handles
    // reviewRequested(), rereading the staged changes before a new run.
    explicit ReviewView(ReviewSession *session, QWidget *parent = nullptr);

    // The owned or borrowed session, for its send/resolve signals.
    ReviewSession *session() const { return session_; }

signals:
    // A finding activated in the list: its place; `line` from 1.
    void openRequested(const QString &path, int line);
    void findingRequested(const ReviewReport::Finding &finding);
    // Close the review; Stop review keeps the interruption visible.
    void closeRequested();
    void reviewRequested();
    // Returns to the code or staged diff without discarding the review.
    void inspectRequested();

private:
    // The page of the outcomes, under the header.
    QWidget *createResults();
    QWidget *createCleanResult();
    QLayout *createHeader();
    void onStateChanged(ReviewSession::State state);
    // The results page, nothing shown on it yet but the header.
    void showResults();
    void showCheckFailure();
    void showFinished();
    // Keeps a dismissed report visible so its findings can be restored.
    void showFindings();

    ReviewSession *session_;

    QStackedLayout *pages_;
    ReviewProgressView *progress_;
    QWidget *results_ = nullptr;
    QWidget *clean_ = nullptr;
    QWidget *resultsColumn_ = nullptr;
    QWidget *resultActions_ = nullptr;
    QVBoxLayout *resultsLayout_ = nullptr;
    QHBoxLayout *headingLayout_ = nullptr;
    QLabel *cleanSummary_ = nullptr;
    QLabel *cleanElapsed_ = nullptr;
    QLabel *cleanWarning_ = nullptr;
    QLabel *failureSymbol_ = nullptr;
    QLabel *title_;
    QLabel *kind_;
    QLabel *elapsedLabel_;
    QLabel *summary_;
    QLabel *handoffNote_ = nullptr;
    ReviewFindingsView *findings_;
    QTextBrowser *rawAnswerView_; // also the output of a failing check
    QLabel *error_;
    QPushButton *reviewAnyway_;
    QPushButton *retry_;
    QPushButton *sendAll_; // of an answer that is not a report
    QPushButton *close_;
};
