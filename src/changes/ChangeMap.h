// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/Theme.h"

#include <QWidget>

#include <utility>

class DiffView;

// A map of a DiffView's changes, in a narrow column beside it: every added
// and removed line at its place in the whole diff, in green and red, the
// files apart by a rule, and the part in view framed. A click or a drag
// scrolls the view to that place.
class ChangeMap : public QWidget {
    Q_OBJECT

public:
    // Follows `diff`, which must outlive it.
    explicit ChangeMap(DiffView *diff, QWidget *parent = nullptr);

    void applyTheme(const Theme &theme);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    // Pixels per displayed line of the diff.
    qreal scale() const;
    // The view's lines in view: the first, and how many.
    std::pair<int, int> linesInView() const;
    // Centers the view on the line at height `y`.
    void scrollTo(qreal y);
    void paintChanges(QPainter &painter) const;

    DiffView *diff_;
    QColor background_;
    QColor added_;
    QColor removed_;
    QColor rule_;
    QColor frame_;
};
