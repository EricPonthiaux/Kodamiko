// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/Theme.h"

#include <QFrame>

class QToolButton;

// What is left of a pane once folded away (the Changes view's file column,
// the History view's commits): a thin bar, the button that brings the pane
// back at its top, the pane's name written down the bar under it.
class FoldedPane : public QFrame {
    Q_OBJECT

public:
    // The button shows `iconName`, one of the UiIcons, and says `toolTip`.
    FoldedPane(QString title, QString iconName, const QString &toolTip, QWidget *parent = nullptr);

    void applyTheme(const Theme &theme);
    // The button centered in the top `height` pixels, as in the pane bars
    // beside it (their buttons at the same height).
    void setButtonRowHeight(int height);

signals:
    void expandRequested();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString title_;
    QString iconName_;
    QToolButton *expandButton_;
};
