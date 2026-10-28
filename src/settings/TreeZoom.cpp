// SPDX-License-Identifier: GPL-3.0-only

#include "TreeZoom.h"

#include "Preferences.h"
#include "ui/AppFonts.h"
#include "ui/TreeMetrics.h"

#include <QTreeView>
#include <QWheelEvent>

#include <algorithm>

void TreeZoom::setFont(QTreeView *tree, const QFont &font)
{
    auto *zoom = tree->findChild<TreeZoom *>(QString(), Qt::FindDirectChildrenOnly);
    if (!zoom)
        zoom = new TreeZoom(tree);
    zoom->baseFont_ = font;
    zoom->apply();
}

TreeZoom::TreeZoom(QTreeView *tree)
    : QObject(tree)
    , tree_(tree)
{
    // The wheel reaches the viewport first.
    tree->viewport()->installEventFilter(this);
}

bool TreeZoom::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::Wheel)
        return QObject::eventFilter(watched, event);
    const auto *wheel = static_cast<QWheelEvent *>(event);
    const int delta = wheel->angleDelta().y();
    // Rounded: the size converted to the system's points and back is 13.99….
    const int size = qRound(AppFonts::kdkPoints(baseFont_.pointSizeF()));
    const bool isZoom = wheel->modifiers() & Qt::ControlModifier;
    if (!isZoom || delta == 0 || size <= 0)
        return QObject::eventFilter(watched, event);
    const int zoomPoints = std::clamp(zoomPoints_ + (delta > 0 ? 1 : -1), Preferences::minimumFontSize - size,
                                      Preferences::maximumFontSize - size);
    if (zoomPoints != zoomPoints_) {
        zoomPoints_ = zoomPoints;
        apply();
    }
    return true; // not a scroll by pages, Qt's Ctrl+wheel
}

void TreeZoom::apply()
{
    QFont font = baseFont_;
    if (font.pointSizeF() > 0)
        font.setPointSizeF(AppFonts::systemPoints(AppFonts::kdkPoints(font.pointSizeF()) + zoomPoints_));
    applyTreeMetrics(tree_, font);
}
