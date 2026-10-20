// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/OverlayPopup.h"
#include "ui/Theme.h"

class PopupHeader;
class QLineEdit;
class QPushButton;

// Claude ▸ Check Before Merging Agents, a small popup over the window: the
// project's shell command run in an agent's worktree before its commits are
// merged (see AgentMergeQueue).
class AgentCheckPopup : public OverlayPopup {
    Q_OBJECT

public:
    AgentCheckPopup(const Theme &theme, QWidget *parent);

    // Shows the popup with the project's `command` in the field.
    void popup(const QString &command);
    void applyTheme(const Theme &theme);

    QLineEdit *commandField() const { return command_; }

    // Emits saved with the command, trimmed.
    void save();

signals:
    // Empty: no check.
    void saved(const QString &command);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    PopupHeader *header_;
    QLineEdit *command_;
};
