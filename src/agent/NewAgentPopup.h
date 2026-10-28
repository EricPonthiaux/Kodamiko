// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/OverlayPopup.h"
#include "ui/Theme.h"

class AgentProvider;
class PopupHeader;
class QLabel;
class QLineEdit;
class QPushButton;

// Agent ▸ New Session in Worktree, a small popup over the window:
// the name of the session, which also names its worktree and its branch
// (kodamiko/<name>), checked as it is typed.
class NewAgentPopup : public OverlayPopup {
    Q_OBJECT

public:
    // Whether the checkout has changes the new worktree will lack.
    enum class UncommittedChanges {
        None,
        Some,
    };

    NewAgentPopup(const Theme &theme, QWidget *parent);

    // Shows the popup with `suggestedName` in the field.
    void popup(const QString &suggestedName, UncommittedChanges changes);
    // popup() without showing, for tests.
    void load(const QString &suggestedName, UncommittedChanges changes);
    void applyTheme(const Theme &theme);
    // The agent the session runs: its name in the title, its icon from the
    // next applyTheme().
    void setProvider(const AgentProvider &provider);

    QLineEdit *nameField() const { return name_; }

    QPushButton *startButton() const { return start_; }

    QLabel *message() const { return message_; }

    // Emits startRequested if the name is valid.
    void start();

signals:
    void startRequested(const QString &name);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void onNameEdited(const QString &text);
    void validate();

    PopupHeader *header_;
    QLineEdit *name_;
    QLabel *message_;
    QLabel *changesNote_;
    QPushButton *start_;
};
