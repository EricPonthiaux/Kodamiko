// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "terminal/OptionKey.h"

#include <QStringList>
#include <QWidget>

class QCheckBox;
class QListWidget;
class QPushButton;

// The content of Preferences ▸ Terminal: the folders put first in the PATH
// of every program Kodamiko launches in a terminal (shells, run
// configurations, agent sessions), in their order, each editable in place;
// on macOS, what the Option key does.
class TerminalPreferencesPage : public QWidget {
    Q_OBJECT

public:
    explicit TerminalPreferencesPage(QWidget *parent = nullptr);

    void setPathFolders(const QStringList &folders);
    // The folders as listed, without the empty ones.
    QStringList pathFolders() const;

    void setOptionKey(OptionKey optionKey);
    OptionKey optionKey() const;

    // The list. For tests.
    QListWidget *folderList() const { return folders_; }

private:
    QWidget *createButtons();
    QWidget *createKeyboardSection();
    void addFolder();
    void removeFolder();
    // Moves the selected folder `rows` rows down (up when negative).
    void moveFolder(int rows);
    void updateButtons();

    QListWidget *folders_;
    QPushButton *remove_ = nullptr;
    QPushButton *up_ = nullptr;
    QPushButton *down_ = nullptr;
    QCheckBox *optionAsMeta_ = nullptr;
};
