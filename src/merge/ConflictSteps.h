// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ConflictState.h"
#include "ui/Theme.h"

#include <QList>
#include <QWidget>

// A dot per conflict, in its state's color, the one shown ringed; a click
// goes to it. Hidden without conflicts, or with too many to tell apart.
class ConflictSteps : public QWidget {
    Q_OBJECT

public:
    explicit ConflictSteps(QWidget *parent);

    void setTheme(const Theme &theme);
    // `current` indexes `states`; -1 for none.
    void setConflicts(const QList<ConflictState> &states, int current);

    QSize sizeHint() const override;

signals:
    void conflictRequested(int index);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRectF dotRect(int index) const;
    // -1 outside every dot.
    int indexAt(QPoint position) const;
    void paintDot(QPainter &painter, int index) const;

    QList<ConflictState> states_;
    int current_ = -1;
    Theme theme_;
};
