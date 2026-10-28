// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewSession.h"

#include <QWidget>

class QLabel;
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

    // Owned by the view; to connect to sendRequested().
    ReviewSession *session() const { return session_; }

signals:
    // A finding activated in the list: its place; `line` from 1.
    void openRequested(const QString &path, int line);
    // Cancel or Close.
    void closeRequested();

private:
    // The page of the outcomes, under the header.
    QWidget *createResults();
    QLayout *createHeader();
    void onStateChanged(ReviewSession::State state);
    // The results page, nothing shown on it yet but the header.
    void showResults();
    void showCheckFailure();
    void showFinished();
    // The findings, while there are some.
    void showFindings();

    ReviewSession *session_;

    QStackedLayout *pages_;
    ReviewProgressView *progress_;
    QWidget *results_ = nullptr;
    QLabel *title_;
    QLabel *elapsedLabel_;
    QLabel *summary_;
    ReviewFindingsView *findings_;
    QTextBrowser *rawAnswerView_; // also the output of a failing check
    QLabel *error_;
    QPushButton *reviewAnyway_;
    QPushButton *retry_;
    QPushButton *sendAll_; // of an answer that is not a report
    QPushButton *close_;
};
