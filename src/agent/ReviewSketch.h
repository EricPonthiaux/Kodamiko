// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QElapsedTimer>
#include <QWidget>

class QTimer;

// A page of code being read, over and over, while an agent reviews the real
// one: a band moves down the blank lines, the lines it passed brighten, and
// some get a finding as it leaves them, a warning or an error mark in the
// gutter and a wavy underline; the page then fades and the reading starts
// again. It shows the page read, still, when motion is turned off
// (Motion::policy()) or once the review is over.
class ReviewSketch : public QWidget {
    Q_OBJECT

public:
    enum class State {
        Reading,
        Done,
        Failed
    };

    explicit ReviewSketch(QWidget *parent);

    // Reading loops from the first line; Done shows the page read with its
    // findings, Failed the same in the placeholder color.
    void setState(State state);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateMotion();
    // Where the loop stands, from 0 to 1.
    qreal cycleProgress() const;

    QTimer *frame_;
    QElapsedTimer clock_;
    State state_ = State::Done;
};
