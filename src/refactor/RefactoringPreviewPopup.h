// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "RefactoredFile.h"
#include "changes/DiffLayout.h"
#include "lsp/LspProtocol.h"
#include "ui/OverlayPopup.h"

#include <functional>

class DiffView;
class PopupHeader;
class QListWidget;
class QPushButton;

// A refactoring before it is made, in a popup over the window: the files
// it changes, each checked, and the diff of the selected one. A file with
// only text edits can be unchecked to leave it as it is; one the
// refactoring creates, renames or deletes cannot. Do Refactor makes the
// changes of the checked files, Cancel or Escape none. The popup follows
// the applied theme and deletes itself once dismissed.
class RefactoringPreviewPopup : public OverlayPopup {
    Q_OBJECT

public:
    struct Preview {
        QString title; // "Rename “total” to “product”"
        QString root;  // the paths are shown relative to it
        LspProtocol::WorkspaceEdit edit;
        QList<RefactoredFile> files; // EditPreview::simulate(edit)
    };

    // Receives the edit without the unchecked files.
    using Apply = std::function<void(const LspProtocol::WorkspaceEdit &edit)>;

    RefactoringPreviewPopup(Preview preview, Apply apply, QWidget *parent);

    // Shows the popup, the first file selected.
    void popup();
    // Do Refactor.
    void accept();

    QListWidget *files() const { return list_; }

    DiffView *diff() const { return diff_; }

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void showSelected();
    void updateButton();

    Preview preview_;
    Apply apply_;
    QList<DiffFile> diffFiles_; // by row of the list

    PopupHeader *header_;
    QListWidget *list_;
    DiffView *diff_;
    QPushButton *refactor_;
};
