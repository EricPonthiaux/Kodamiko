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

class MergeTextPanel;
class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;

// The conflicts of the operation in progress (merge, rebase, cherry-pick,
// revert, a stash applied) to resolve: the conflicted files at the left,
// the chosen one at the right, in three columns for a text file
// (MergeTextPanel). Each file keeps its resolution while the view lives.
class MergeView : public QWidget {
    Q_OBJECT

public:
    MergeView(QString folder, Theme theme, QWidget *parent = nullptr);
    ~MergeView() override;

    // Reads the conflicts again, off the UI thread.
    void reload();
    void applyTheme(const Theme &theme);

private:
    // What reload() reads.
    struct Conflicts {
        RepositoryInfo repository;
        QList<ConflictVersions::File> files;
        Integration::ConflictSides sides;
        QString error;
    };

    QWidget *createBar();
    void showConflicts(const Conflicts &conflicts);
    void showFile(int row);
    void showMessage(const QString &message);
    void updateList();
    void updateBar();
    QString titleOf(const Conflicts &conflicts) const;
    QString stateOf(const ConflictVersions::File &file) const;
    // The resolution of `file`, made on first use; nullptr for a conflict
    // that is not text in both versions.
    MergeDocument *documentOf(const ConflictVersions::File &file);

    QString folder_;
    Theme theme_;
    Conflicts conflicts_;
    std::map<QString, std::unique_ptr<MergeDocument>> documents_;
    QLabel *title_ = nullptr;
    QLabel *summary_ = nullptr;
    QPushButton *previous_ = nullptr;
    QPushButton *next_ = nullptr;
    QListWidget *list_ = nullptr;
    QVBoxLayout *fileArea_ = nullptr;
    QLabel *message_ = nullptr;
    QPointer<MergeTextPanel> panel_;
    // Last: it waits for its reads before the members they report to go.
    QThreadPool pool_;
};
