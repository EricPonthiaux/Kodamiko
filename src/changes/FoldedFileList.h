// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/Theme.h"

#include <QFrame>

class QToolButton;

// What is left of the Changes view's file column once folded away: a thin
// bar along the view's left edge, the button that brings the column back
// at its top.
class FoldedFileList : public QFrame {
    Q_OBJECT

public:
    explicit FoldedFileList(QWidget *parent = nullptr);

    void applyTheme(const Theme &theme);

signals:
    void expandRequested();

private:
    QToolButton *expandButton_;
};
