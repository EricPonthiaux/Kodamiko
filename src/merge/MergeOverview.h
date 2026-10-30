// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ConflictState.h"
#include "ui/Theme.h"

#include <QList>
#include <QWidget>

// The whole length of the file shown, beside the merge view's columns: a
// mark per conflict in its state's color, the one shown outlined, and a
// frame around the rows in view. A click on a mark goes to its conflict,
// elsewhere to that place of the file.
class MergeOverview : public QWidget {
    Q_OBJECT

public:
    struct Conflict {
        int firstRow = 0;
        int rows = 0;
        ConflictState state = ConflictState::Open;
    };

    struct Map {
        QList<Conflict> conflicts;
        int current = -1; // indexes `conflicts`
        int totalRows = 0;
        int firstVisibleRow = 0;
        int visibleRows = 0;
    };

    MergeOverview(const Theme &theme, QWidget *parent);

    void setMap(const Map &map);
    void applyTheme(const Theme &theme);
    QSize sizeHint() const override;

signals:
    void conflictRequested(int index);
    void rowRequested(int row);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRectF markRect(const Conflict &conflict) const;
    qreal yOf(int row) const;

    Map map_;
    Theme theme_;
};
