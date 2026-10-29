// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/Theme.h"

#include <QWidget>

// Two sheets of code joined by a Git branch, at rest. Only for a repository
// with no pending changes or operation; not for an empty selection or error.
// Painted in the current theme, without animation, and fitted to the pane.
class CleanDiffState : public QWidget {
    Q_OBJECT

public:
    CleanDiffState(Theme theme, QWidget *parent);

    void applyTheme(const Theme &theme);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Theme theme_;
};
