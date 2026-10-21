// SPDX-License-Identifier: GPL-3.0-only

#include "SideBySideDivider.h"

#include "ui/GripSplitter.h"

#include <QMouseEvent>
#include <QPainter>

SideBySideDivider::SideBySideDivider(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::SplitHCursor);
    setToolTip(tr("Drag to share the width between the two versions"));
}

bool SideBySideDivider::event(QEvent *event)
{
    if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave)
        hover_.setHovered(event->type() == QEvent::HoverEnter);
    return QWidget::event(event);
}

void SideBySideDivider::mousePressEvent(QMouseEvent *event)
{
    event->accept(); // the drag is ours, not the text's below
}

void SideBySideDivider::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
        emit dragged(mapToParent(event->position().toPoint()).x());
}

void SideBySideDivider::paintEvent(QPaintEvent *)
{
    // The splitters' grip, halfway down, on the gap between the sides.
    QPainter painter(this);
    SplitterGrip::paint(painter, *this, Qt::Horizontal, hover_.progress());
}
