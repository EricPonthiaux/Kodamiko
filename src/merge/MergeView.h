// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "MergeDocument.h"
#include "git/ConflictVersions.h"
#include "git/Integration.h"
#include "ui/Theme.h"

#include <QPointer>
#include <QThreadPool>
#include <QWidget>

#include <map>
#include <memory>

class GitCommands;
class MergeTextPanel;
class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;

// The conflicts of the operation in progress (merge, rebase, cherry-pick,
// revert, a stash applied) to resolve: the conflicted files at the left,
// the chosen one at the right, in three columns for a text file
// (MergeTextPanel). Each file keeps its resolution until it is saved, which
// marks it resolved, or until git's versions of it change. Once no file is
// left, the operation goes on from the view.
class MergeView : public QWidget {
    Q_OBJECT

public:
    // `commands`, not owned, runs the saves; it must outlive the view.
    MergeView(QString folder, Theme theme, GitCommands *commands, QWidget *parent = nullptr);
    ~MergeView() override;

    // Reads the conflicts again, off the UI thread.
    void reload();
    void applyTheme(const Theme &theme);

signals:
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
        QString error;

        bool operator==(const Conflicts &) const = default;
    };

    // A file being resolved, with the versions its document was made from.
    struct Resolution {
        ConflictVersions::File file;
        std::unique_ptr<MergeDocument> document;
    };

    QWidget *createBar();
    QWidget *createFooter();
    void showConflicts(const Conflicts &conflicts);
    void showFile(int row);
    void showMessage(const QString &message);
    void saveCurrentFile();
    void updateList();
    void updateBar();
    void updateFooter();
    const ConflictVersions::File *currentFile() const;
    // The resolution of `file`, made on first use; nullptr for a conflict
    // that is not text in both versions.
    MergeDocument *documentOf(const ConflictVersions::File &file);
    const MergeDocument *openedDocument(const QString &path) const;

    QString folder_;
    Theme theme_;
    GitCommands *commands_;
    Conflicts conflicts_;
    bool hasRead_ = false;
    std::map<QString, Resolution> resolutions_;
    QLabel *title_ = nullptr;
    QLabel *summary_ = nullptr;
    QPushButton *previous_ = nullptr;
    QPushButton *next_ = nullptr;
    QListWidget *list_ = nullptr;
    QVBoxLayout *fileArea_ = nullptr;
    QLabel *message_ = nullptr;
    QPointer<MergeTextPanel> panel_;
    QLabel *hint_ = nullptr;
    QPushButton *save_ = nullptr;
    QPushButton *abort_ = nullptr;
    QPushButton *continue_ = nullptr;
    QPushButton *close_ = nullptr;
    // Last: it waits for its reads before the members they report to go.
    QThreadPool pool_;
};
