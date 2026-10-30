// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"

#include <QWidget>

// The welcome brand panel: the monogram, a wordmark signed with a still caret,
// and the editor/agent signature. Its typography follows the application font
// and its colors follow the theme. No animation.
class BrandHero : public QWidget {
    Q_OBJECT

public:
    BrandHero(Theme theme, QWidget *parent);

    // Repaints the illustration and its copy in the current theme.
    void applyTheme(const Theme &theme);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Theme theme_;
};
