// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "MergeDocument.h"
#include "MergeRequest.h"
#include "git/ConflictVersions.h"
#include "git/Integration.h"
#include "ui/Theme.h"

#include <QPointer>
#include <QThreadPool>
#include <QWidget>

#include <functional>
#include <map>
#include <memory>

class AgentProvider;
class ConflictNavigator;
class GitCommands;
class MergeProposals;
class MergeChoicePanel;
class MergeTextPanel;
class MergeReadyPanel;
class DiffView;
class QLabel;
class QListWidget;
class QPushButton;
class QToolButton;
class QVBoxLayout;
class QStackedWidget;
class ViewHeader;

// The conflicts of the operation in progress (merge, rebase, cherry-pick,
// revert, a stash applied) to resolve: the conflicted files at the left,
// the chosen one at the right, in three columns for a text file
// (MergeTextPanel), as a choice otherwise (MergeChoicePanel). A text file
// keeps its resolution until it is saved, which marks it resolved, or until
// git's versions of it change. Once no file is left, the operation goes on
// from the view. The agent, asked, proposes resolutions for the open
// conflicts of the file shown (MergeProposals).
class MergeView : public QWidget {
    Q_OBJECT

public:
    // `commands`, not owned, runs the saves; it must outlive the view.
    MergeView(QString folder, Theme theme, GitCommands *commands, QWidget *parent = nullptr);
    ~MergeView() override;

    // Reads the conflicts again, off the UI thread.
    void reload();
    void applyTheme(const Theme &theme);
    // The agent that proposes resolutions, not owned; null for none.
    void setAgent(const AgentProvider *agent);
    // The reviewed message for a merge; other operations keep Git's message.
    std::optional<QString> mergeMessage() const;
    // Git is continuing the operation; prevents editing and shows progress.
    void setFinalizing(bool finalizing);

signals:
    // A merge completed outside this view (for example from the agent).
    // Verified against the resulting commit's parents, not just an empty index.
    void mergeCompleted(const Integration::Completion &completion);
    // Git cleared its operation without a merge commit (abort or quit).
    void operationEnded();
    // The operation in progress: go on, or give up.
    void continueRequested();
    void abortRequested();
    // No operation and no conflict left: the view has nothing to show.
    void closeRequested();

private:
    // What reload() reads.
    struct Conflicts {
        RepositoryInfo repository;
        QList<ConflictVersions::File> files;
        Integration::ConflictSides sides;
        QStringList paths;
        QList<FileDiff> staged;
        QString message;
        QStringList mergeHeads;
        bool completedMerge = false;
        QString error;

        bool operator==(const Conflicts &) const = default;
    };

    // A file being resolved, with the versions its document was made from.
    struct Resolution {
        ConflictVersions::File file;
        std::unique_ptr<MergeDocument> document;
    };

    QWidget *createTitleBar();
    QWidget *createSidebar();
    QWidget *createBar();
    QWidget *createFooter();
    static Conflicts readConflicts(const QString &folder, const Conflicts &previous);
    static void readReadyState(Conflicts &conflicts);
    static bool completesMerge(const Conflicts &previous, const Conflicts &current);
    void showReady();
    void toggleReview();
    void showConflicts(const Conflicts &conflicts);
    void rememberPaths(const Conflicts &conflicts);
    void rebuildFiles();
    void showConflictState(const QString &selected);
    void showFile(int row);
    // A conflict without lines to combine: a choice resolves it.
    void showChoices(const ConflictVersions::File &file);
    void showMessage(const QString &message);
    // Runs `job`, which resolves files in git, then reads the conflicts again.
    void resolve(const QString &title, const std::function<QString()> &job);
    void saveCurrentFile();
    // In the file shown, the conflicts that need no one to decide.
    void resolveSimpleConflicts();
    // The agent's proposals for the file shown (MergeViewAgent.cpp).
    void connectProposals();
    void askOrStopAgent();
    void showProposals(const QString &path, const MergeRequest::Proposals &proposals);
    void updateAgentButton();
    void updateList();
    void updateBar();
    void updateNavigator();
    void onConflictResolved(int index, int next);
    void updateFooter();
    void updateFinishButtons(bool isDone, bool hasOperation, bool busy);
    QString footerHint(const MergeDocument *document, bool isDone) const;
    const ConflictVersions::File *currentFile() const;
    // The resolution of `file`, made on first use; nullptr for a conflict
    // that is not text in both versions.
    MergeDocument *documentOf(const ConflictVersions::File &file);
    const MergeDocument *openedDocument(const QString &path) const;

    QString folder_;
    Theme theme_;
    GitCommands *commands_;
    const AgentProvider *agent_ = nullptr;
    MergeProposals *proposals_;
    Conflicts conflicts_;
    bool hasRead_ = false;
    bool finalizing_ = false;
    QStringList mergedPaths_;
    std::map<QString, Resolution> resolutions_;
    QLabel *title_ = nullptr;
    QLabel *summary_ = nullptr;
    ViewHeader *filesHeader_ = nullptr;
    ViewHeader *fileHeader_ = nullptr;
    QLabel *fileTitle_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QWidget *working_ = nullptr;
    MergeReadyPanel *ready_ = nullptr;
    DiffView *review_ = nullptr;
    QPushButton *reviewFiles_ = nullptr;
    QToolButton *resolveSimple_ = nullptr;
    QToolButton *askAgent_ = nullptr;
    ConflictNavigator *navigator_ = nullptr;
    QListWidget *list_ = nullptr;
    QVBoxLayout *fileArea_ = nullptr;
    QLabel *message_ = nullptr;
    QPointer<MergeTextPanel> panel_;
    QPointer<MergeChoicePanel> choices_;
    QLabel *hint_ = nullptr;
    QPushButton *save_ = nullptr;
    QPushButton *abort_ = nullptr;
    QPushButton *continue_ = nullptr;
    QPushButton *close_ = nullptr;
    // Last: it waits for its reads before the members they report to go.
    QThreadPool pool_;
};
