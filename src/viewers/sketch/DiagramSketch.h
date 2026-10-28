// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "SketchScene.h"

#include <QElapsedTimer>
#include <QWidget>

class QTimer;

// A diagram of a given kind that draws itself, over and over, while an agent
// writes the real one: blank shapes appear one after the other on a dot grid,
// a pen traces the links between them, then the whole sketch fades and starts
// again (SketchScenes holds a scene per kind). It shows the finished sketch,
// still, when motion is turned off (Motion::policy()) or once the drawing is
// over.
class DiagramSketch : public QWidget {
    Q_OBJECT

public:
    enum class State {
        Drawing,
        Done,
        Failed
    };

    // A sketch of the diagram family `kindId` (DiagramKind::id).
    DiagramSketch(QStringView kindId, QWidget *parent);

    // Drawing loops from the first shape; Done shows the full sketch in the
    // accent color, Failed in the placeholder color.
    void setState(State state);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateMotion();
    // Where the loop stands, from 0 to 1.
    qreal cycleProgress() const;
    // How far stroke `index` is drawn at `cycle`, from 0 to 1.
    qreal strokeProgress(qsizetype index, qreal cycle) const;
    void drawScene(QPainter &painter, qreal cycle) const;

    SketchScene scene_;
    QTimer *frame_;
    QElapsedTimer clock_;
    State state_ = State::Done;
};
