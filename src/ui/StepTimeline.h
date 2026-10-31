// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QElapsedTimer>
#include <QStringList>
#include <QWidget>

class QTimer;

// The last steps of an agent's run, one per line on a vertical rail: a check
// for each step done, and a pulsing ring for the current one, or a cross
// once the run failed. Long steps are elided.
class StepTimeline : public QWidget {
    Q_OBJECT

public:
    // Only Running moves: Done checks the last step as the others, Failed
    // crosses it.
    enum class State {
        Running,
        Done,
        Failed
    };

    explicit StepTimeline(QWidget *parent);

    // Forgets the steps and runs again.
    void clear();
    void addSteps(const QStringList &steps);
    void setState(State state);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void updateMotion();
    int rowHeight() const;
    void drawMarker(QPainter &painter, QPointF center, bool isCurrent) const;

    QStringList steps_;
    State state_ = State::Running;
    QTimer *frame_;
    QElapsedTimer clock_;
};
