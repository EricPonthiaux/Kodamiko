// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ConflictState.h"
#include "ui/Theme.h"

#include <QList>
#include <QRect>
#include <QWidget>

class QVariantAnimation;

// Drawn over the merge view's three columns, letting the mouse through: a
// frame across the columns around the conflict shown, the other conflicts
// dimmed, the result of the conflicts still open hatched, and a tag
// ("2 / 3") on the frame. A halo marks the arrival at a conflict.
class MergeFocusFrame : public QWidget {
    Q_OBJECT

public:
    struct Band {
        QRect area;   // across the three columns, in this widget
        QRect result; // the result's part of it
        ConflictState state = ConflictState::Open;
        QString tag; // shown on the current band only
        bool isCurrent = false;
    };

    MergeFocusFrame(const Theme &theme, QWidget *parent);

    // `visible` is where the columns show code: the bands are cut to it.
    void setBands(const QList<Band> &bands, const QRect &visible);
    // The halo around the band shown, fading away.
    void flash();
    void applyTheme(const Theme &theme);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void paintBand(QPainter &painter, const Band &band) const;
    void paintTag(QPainter &painter, const Band &band) const;

    QList<Band> bands_;
    QRect visible_;
    Theme theme_;
    QVariantAnimation *halo_;
    qreal haloStrength_ = 0; // 1 as the view arrives, 0 when gone
};
