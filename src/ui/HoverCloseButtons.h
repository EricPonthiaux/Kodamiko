// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>

class QTabBar;

// Shows the close buttons of `bar`'s tabs only on the current tab and on the
// tab under the mouse, as VS Code does: rows of many tabs stay light. The
// hidden buttons keep their place, so the tabs do not change width, and
// they are transparent rather than hidden: QTabBar shows its tab buttons
// again whenever it lays them out.
class HoverCloseButtons : public QObject {
    Q_OBJECT

public:
    // Owned by `bar`.
    explicit HoverCloseButtons(QTabBar *bar);

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void update();

    QTabBar *bar_;
    int hovered_ = -1; // the tab under the mouse, -1 if none
};
