// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "OverlayPopup.h"
#include "Theme.h"

class PopupHeader;
class QLabel;
class QPushButton;

// A message the user reads before going on (a merge that failed, a command
// that cannot run), in a popup over the window rather than a message box: a
// header with the icon and the title, the text, a button to close it. The
// messages arriving while one is shown wait their turn: closing it shows the
// next.
class MessagePopup : public OverlayPopup {
    Q_OBJECT

public:
    // `iconName` is one of UiIcons'; `settingsKey` keeps the size the user
    // gives it.
    MessagePopup(QString iconName, const QString &settingsKey, const Theme &theme, QWidget *parent);

    // Shows `text` under `title`, after the messages already queued; nothing
    // if it repeats the last of them.
    void popup(const QString &title, const QString &text);
    // popup() without showing, for tests.
    void load(const QString &title, const QString &text);
    void applyTheme(const Theme &theme);
    // Shown from the next applyTheme().
    void setIconName(const QString &iconName);

    // The message shown, empty when there is none.
    QString text() const;
    // The messages waiting after the one shown.
    qsizetype waitingCount() const;

    // Closes the message shown: the next one takes its place, or the popup
    // hides.
    void next();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct Message {
        QString title;
        QString text;

        bool operator==(const Message &) const = default;
    };

    void showFirst();

    // The first is shown, the others wait.
    QList<Message> queue_;
    PopupHeader *header_;
    QLabel *text_;
    QPushButton *close_;
};
