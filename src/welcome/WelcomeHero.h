// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/Theme.h"

#include <QWidget>

// The left side of the welcome window ("Constellation", see
// docs/mockups/welcome.html): Kodamiko's mark as a sculpture of light, its
// thin caret (you) beside its block (Claude), at the centre of fine orbits
// on a field of stars, above a headline. Painted in the colors of the
// theme, which it follows as the mark does: the caret in the text color,
// the block in the accent. Still: no animation.
class WelcomeHero : public QWidget {
    Q_OBJECT

public:
    WelcomeHero(Theme theme, QWidget *parent);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Theme theme_;
};
