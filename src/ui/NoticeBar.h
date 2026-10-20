// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"

#include <QFrame>

class QLabel;
class QPushButton;
class QToolButton;

// A message across the top of the editor, for something the user should
// know but that does not stop the work (a language server that cannot
// start): an icon, the text, an optional action and a close button.
class NoticeBar : public QFrame {
    Q_OBJECT

public:
    explicit NoticeBar(QWidget *parent = nullptr);

    // Shows `text`; `action` names the button (none when empty).
    void showNotice(const QString &text, const QString &action = {});
    QString text() const;
    void applyTheme(const Theme &theme);

signals:
    void actionClicked();
    // The close button.
    void dismissed();

private:
    QLabel *icon_;
    QLabel *text_;
    QPushButton *action_;
    QToolButton *close_;
};
