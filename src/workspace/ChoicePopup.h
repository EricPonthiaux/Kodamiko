// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "EntryPopups.h"

#include <QList>

#include <functional>

// Asks the user to choose between actions: a message, optional details in a
// quieter color, then Cancel and one button per action, the first focused.
// Enter presses the focused button. Without actions, it only tells: a Close
// button (see showNotice()).
class ChoicePopup : public EntryPopup {
    Q_OBJECT

public:
    struct Question {
        QString title;
        QString location; // under the title: where it applies; may be empty
        QString message;
        QString details; // may be empty
    };

    struct Choice {
        QString text; // the button
        std::function<void()> run;
    };

    // The popup closes once a choice ran; cancelling runs none.
    ChoicePopup(const Question &question, const QList<Choice> &choices, QWidget *parent);

    // Renames Cancel ("Keep" when the action discards something).
    void setCancelText(const QString &text);
    // Focuses Cancel rather than the first action, for a destructive one.
    void focusCancel();

    QPushButton *cancelButton() const { return cancel_; }

    QList<QPushButton *> choiceButtons() const { return buttons_; }

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void accept() override;
    void focusContent() override;

private:
    void choose(qsizetype index);

    QList<Choice> choices_;
    QList<QPushButton *> buttons_;
    QPushButton *cancel_;
    bool focusesCancel_ = false;
};

// Tells the user `question` in a popup over `parent`'s window, closed with
// Close, Enter or Escape; returns at once.
void showNotice(const ChoicePopup::Question &question, QWidget *parent);
