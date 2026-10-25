// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewSession.h"

#include <QFrame>
#include <QPointer>

class QLabel;
class QPushButton;
class QTextBrowser;
class QTimer;
class Spinner;

// A review (ReviewSession) told in a bar over the diff whose lines show its
// findings: what the reviewer does as it goes, then how many findings it
// left, a button that shows them over the diff (ReviewFindingsPanel), Send
// All to <agent> and Review Again. A failing check shows its last lines,
// with Review Anyway; an answer that is not a report shows as Markdown.
class ReviewBar : public QFrame {
    Q_OBJECT

public:
    explicit ReviewBar(QWidget *parent = nullptr);

    // Follows `session`, not owned; null: none, the bar hidden.
    void setSession(ReviewSession *session);

signals:
    // The findings' button: the findings over the diff.
    void findingsRequested();
    // Cancel or Close: the review is over.
    void closeRequested();
    // Review Again: a new review of what the diff shows now, the session's
    // patch being that of the changes as they were.
    void reviewAgainRequested();

private:
    QLayout *createButtons();
    void onStateChanged(ReviewSession::State state);
    void showProgress();
    void showCheckFailure();
    void showFinished();
    void showFailure();
    void showLastStep();
    // The findings' button and Send All, for the findings left.
    void updateFindings();
    // `title`, and `message` under it in `color` (empty: the text's).
    void setText(const QString &title, const QString &message, const QString &color = {});
    void updateElapsed();

    QPointer<ReviewSession> session_;
    QTimer *clock_;
    Spinner *spinner_;
    QLabel *title_;
    QLabel *step_; // the reviewer's last step, while it runs
    QLabel *elapsedLabel_;
    QLabel *message_;      // the summary or the error
    QTextBrowser *output_; // a failing check's output, or an answer that is not a report
    QPushButton *findings_;
    QPushButton *cancel_;
    QPushButton *reviewAnyway_;
    QPushButton *retry_;
    QPushButton *sendAll_;
    QPushButton *close_;
};
