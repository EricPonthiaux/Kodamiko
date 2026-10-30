// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/Theme.h"

#include <QList>
#include <QWidget>

// The band between a side's column and the result's (MergeTextPanel): at
// each conflict, an arrow that takes the side's lines into the result, or
// a check mark once the result has them.
class MergeGutter : public QWidget {
    Q_OBJECT

public:
    // Which way the arrows point: from the current side at the left, from
    // the incoming side at the right.
    enum class Direction {
        Right,
        Left
    };

    struct Mark {
        int segment = 0;
        int y = 0; // the top of the conflict's first row, in this widget
        bool isTaken = false;
    };

    MergeGutter(Direction direction, QString sideName, Theme theme, QWidget *parent);

    void setMarks(QList<Mark> marks, int rowHeight);
    void applyTheme(const Theme &theme);
    QSize sizeHint() const override;

signals:
    // The arrow of `segment` clicked: its side's lines, into the result.
    void takeRequested(int segment);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRect markRect(const Mark &mark) const;
    const Mark *markAt(QPoint position) const;

    Direction direction_;
    QString sideName_;
    Theme theme_;
    QList<Mark> marks_;
    int rowHeight_ = 0;
};
