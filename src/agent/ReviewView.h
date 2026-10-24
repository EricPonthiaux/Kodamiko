// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "AgentRun.h"
#include "ReviewReport.h"
#include "ReviewRequest.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QWidget>

#include <memory>
#include <optional>

class AgentProvider;
class ProblemStore;
class QLabel;
class QPushButton;
class QScrollArea;
class QTemporaryDir;
class QTextBrowser;
class QTimer;
class QVBoxLayout;
class ReviewFindingCard;
class Spinner;

// The tab of a review run (AgentReviewRun): the project's check first, when
// it has one, then what the reviewer reads as it goes, then its findings. A
// failing check stops there, as reviewing code that does not build costs
// much for little: Review Anyway goes on, the failure told to the reviewer. The reviewer is a separate agent process
// that starts from a blank conversation and may only read: it judges the code without the reasoning of the session that
// wrote it.
//
// The findings are also problems of the ProblemStore, underlined in the
// editor and listed in the Problems pane, until they are dismissed or the
// tab closes. Send to <agent> hands one finding, or all of them, to the
// session that wrote the code, for the user to decide what to fix.
class ReviewView : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ReviewView)

public:
    // `agent` reviews `subject` in `workDir`.
    struct Request {
        QString workDir;
        QString subject; // what is reviewed, in words: "src/a.cpp, lines 2-3"
        // A unified diff of the changes to review; empty to review the code
        // as it is.
        QByteArray patch;
        QString checkCommand;                 // the project's check, run first; empty for none
        QString model;                        // as the agent names it; empty for its default
        const AgentProvider *agent = nullptr; // not owned, never null
        ProblemStore *problems = nullptr;     // not owned; null: findings in the tab only
    };

    explicit ReviewView(Request review, QWidget *parent = nullptr);
    ~ReviewView() override;

    // Runs the check, then the review; again, from the check.
    void start();

signals:
    // Send to <agent>: `review` is what the reviewer found, in Markdown.
    void sendRequested(const QString &review);
    // A click on a finding's place; `line` from 1.
    void openRequested(const QString &path, int line);
    // Cancel or Close.
    void closeRequested();

private:
    struct Entry {
        ReviewReport::Finding finding;
        ReviewFindingCard *card = nullptr; // owned by the findings' container
    };

    QLayout *createHeader();
    QWidget *createProgress();
    QWidget *createFindingsArea();
    // Back to the progress, nothing else shown.
    void showProgress();
    void onCheckFinished(const ProjectCheck::Result &result);
    void showCheckFailure(const ReviewRequest::Check &check);
    void startReview(const std::optional<ReviewRequest::Check> &check);
    // The patch in a file of its own folder, for the reviewer to read;
    // nullopt (the tab failed) when it cannot be written.
    std::optional<QString> writePatch();
    void onRunFinished(const AgentRun::Outcome &outcome);
    void addSteps(const QStringList &steps);
    void showReport(const ReviewReport::Report &report);
    // An answer that is not the JSON asked for, shown as Markdown.
    void showRawAnswer(const QString &answer);
    void showFinished();
    void addCard(const ReviewReport::Finding &finding);
    void dismiss(ReviewFindingCard *card);
    void clearFindings();
    // The findings left, in the ProblemStore.
    void publishFindings();
    QList<ReviewReport::Finding> findings() const;
    void fail(const QString &error);
    void updateElapsed();

    QString workDir_;
    QString subject_;
    QByteArray patch_;
    QString checkCommand_;
    QString model_;
    const AgentProvider *agent_;
    QPointer<ProblemStore> problems_;
    QString reviewId_;      // the findings' set in the ProblemStore
    QString problemSource_; // what the Problems pane says told them
    ProjectCheck *check_;
    std::optional<ReviewRequest::Check> failedCheck_; // for Review Anyway
    std::unique_ptr<QTemporaryDir> patchFolder_;
    AgentRun *run_;
    QList<Entry> entries_;
    QString rawAnswer_; // when the answer is not the JSON asked for
    QStringList steps_;
    QElapsedTimer elapsed_;
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
