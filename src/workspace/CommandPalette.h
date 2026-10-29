// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/OverlayPopup.h"

#include <QList>

class QAction;
class QLineEdit;
class QLabel;
struct Theme;
class QMenuBar;
class QTreeWidget;

// Ctrl+Shift+P: every menu command, searchable by typing a few letters of
// its name, with its shortcut. Enter runs the selected command.
class CommandPalette : public OverlayPopup {
    Q_OBJECT

public:
    explicit CommandPalette(QWidget *parent);

    // Shows the commands of `menuBar` (enabled actions of its menus and
    // submenus), at the top of the parent window.
    void popup(const QMenuBar *menuBar);
    void applyTheme(const Theme &theme);

    // Fuzzy match: -1 if the letters of `query` do not all appear, in
    // order, in `text`; otherwise a score that favors consecutive letters
    // and word starts. Case insensitive.
    static int matchScore(QStringView text, QStringView query);

    static int matchScore(const QString &text, const QString &query)
    {
        return matchScore(QStringView(text), QStringView(query));
    }

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Command {
        QString name;     // "Git: Stage All"
        QString menuPath; // "Git": taken with the name, as the action's text may change
        QAction *action{};
    };

    void filter();
    void runCurrent();

    QLabel *summary_ = nullptr;
    QLabel *empty_ = nullptr;
    QLineEdit *search_ = nullptr;
    QTreeWidget *list_ = nullptr;
    QList<Command> commands_;
};
