// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/Integration.h"
#include "ui/Theme.h"

#include <QWidget>

class AgentProvider;
class GitActions;
class GitCommands;
class MergeView;
class WindowFrame;
class WindowTitleBar;

// A dedicated, resizable merge window. Closing only hides it, preserving
// resolutions until it is reopened; an explicit abort goes through Git.
// Its owner destroys it after finished()/aborted() or a project change.
class MergeWindow : public QWidget {
    Q_OBJECT

public:
    MergeWindow(const QString &folder, const Theme &theme, GitCommands *commands, QWidget *parent);
    void open();
    void reload();
    void applyTheme(const Theme &theme);
    void setAgent(const AgentProvider *agent);
    void setFrameless(bool frameless);
    // Git's report of an operation that stopped on conflicts, over the files.
    void showStop(const QString &title, const QString &report);

signals:
    void changed();
    // Emitted after hiding the window, only after Git's success callback.
    void finished(const Integration::Completion &completion);
    void aborted();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void continueOperation();
    void complete(const Integration::Completion &completion);
    void abandon();
    bool ended_ = false;
    GitCommands *commands_; // owned by the main window, outlives this one
    GitActions *actions_;
    MergeView *view_;
    WindowFrame *frame_;
    WindowTitleBar *titleBar_;
};
