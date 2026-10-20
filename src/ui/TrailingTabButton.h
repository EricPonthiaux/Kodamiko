// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>

class QAbstractButton;
class QTabWidget;

// Places `button` (a "+" to add a tab) right after the last tab of `tabs`,
// following the tabs as they are added, closed or renamed. When the tabs
// fill the bar, it stays at the bar's right end, in a space kept for it.
// An end button, if any, keeps the very end of the bar, after that space.
// The space is `tabs`' top right corner widget.
class TrailingTabButton : public QObject {
    Q_OBJECT

public:
    TrailingTabButton(QTabWidget *tabs, QAbstractButton *button);

    // `button` at the right end of the bar; it becomes a child of the tabs.
    void setEndButton(QAbstractButton *button);

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void place();

    QTabWidget *tabs_;
    QAbstractButton *button_;
    QAbstractButton *endButton_ = nullptr;
    QWidget *room_;
    bool placing_ = false;
};
