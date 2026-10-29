// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"

#include <QWidget>

// The welcome brand panel: Kodamiko's mark as a sculpture of light, its
// thin caret (you) beside its block (Claude), at the centre of fine orbits
// on a field of stars. Painted in the colors of the
// theme, which it follows as the mark does: the caret in the text color,
// the block in the accent. Still: no animation.
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
