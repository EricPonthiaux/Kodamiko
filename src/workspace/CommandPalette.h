#pragma once

#include "ui/OverlayPopup.h"

#include <QList>

class QAction;
class QLineEdit;
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
        QString name; // "Git: Stage All"
        QAction *action{};
    };

    void filter();
    void runCurrent();

    QLineEdit *search_;
    QTreeWidget *list_;
    QList<Command> commands_;
};
