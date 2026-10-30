// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/Theme.h"

#include <QWidget>

class QPainter;

// The editor area while no file is open: the soft tint of the accent that
// the welcome's brand panel (BrandHero) spreads behind its mark, with the
// mark faded at its heart. Follows the theme; still.
class EmptyEditorBackdrop : public QWidget {
    Q_OBJECT

public:
    EmptyEditorBackdrop(Theme theme, QWidget *parent);

    void applyTheme(const Theme &theme);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    // Kodamiko's mark, faded, centred on `centre`.
    void paintMark(QPainter &painter, QPointF centre) const;

    Theme theme_;
};
