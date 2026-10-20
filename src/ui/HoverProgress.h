// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QVariantAnimation>

class QWidget;

// How far a custom-painted control is into its hover look: 0 at rest, 1
// under the mouse, eased over Motion::hoverDuration. Its paintEvent blends
// the two looks by progress() rather than switching at once.
class HoverProgress {
public:
    // Repaints `widget` at each step. Not owned: a member of `widget`.
    explicit HoverProgress(QWidget *widget);

    // Heads towards 1 when hovered, towards 0 otherwise, from where it is.
    void setHovered(bool hovered);

    bool isHovered() const { return isHovered_; }

    qreal progress() const { return progress_; }

private:
    QVariantAnimation animation_;
    bool isHovered_ = false;
    qreal progress_ = 0;
};
