// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QLineEdit>

class QAction;

// The field at the top of a search popup (Command Palette, Go To, Find in
// Files), as IntelliJ's: flush with the popup's edges, without a frame of
// its own, a magnifier before the text and a line under it. It follows
// the theme applied (ThemeStyle::applied()).
class PopupSearchField : public QLineEdit {
    Q_OBJECT

public:
    explicit PopupSearchField(QWidget *parent);

protected:
    void changeEvent(QEvent *event) override;

private:
    void updateIcon();

    QAction *magnifier_;
};
