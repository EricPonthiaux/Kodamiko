// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/ConflictVersions.h"
#include "git/Integration.h"
#include "ui/Theme.h"

#include <QWidget>

class DiffView;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QVBoxLayout;

// The resolution of a conflict that has no lines to combine, as one choice:
// keep or delete a file one side deleted (with the change the deletion
// would lose), one version or the other of a binary file (images shown),
// or one name for a file each side renamed.
class MergeChoicePanel : public QWidget {
    Q_OBJECT

public:
    MergeChoicePanel(const Theme &theme,
                     const ConflictVersions::File &file,
                     const Integration::ConflictSides &sides,
                     QWidget *parent);

    void applyTheme(const Theme &theme);

signals:
    // The file as `side` has it, deleted if that side deleted it.
    void sideChosen(Integration::ConflictSide side);
    // The renamed file, at `path` (repository path).
    void nameChosen(const QString &path);

private:
    void addKeepOrDelete(const Theme &theme);
    void addVersions();
    void addNames();
    // The change from `base` to `survivor` that a deletion would lose.
    void addChange(const Theme &theme,
                   const ConflictVersions::Version &base,
                   const ConflictVersions::Version &survivor);
    QWidget *createVersion(const QString &caption, const std::optional<ConflictVersions::Version> &version);
    QPushButton *addChoice(const QString &text, const QString &toolTip);

    ConflictVersions::File file_;
    Integration::ConflictSides sides_;
    QVBoxLayout *layout_;
    QLayout *choices_ = nullptr;
    DiffView *change_ = nullptr;
    QRadioButton *currentName_ = nullptr;
    QRadioButton *incomingName_ = nullptr;
    QRadioButton *otherName_ = nullptr;
    QLineEdit *otherPath_ = nullptr;
};
