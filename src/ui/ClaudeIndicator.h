// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "HoverProgress.h"
#include "Theme.h"

#include <QAbstractButton>
#include <QTimer>

// Claude's state in the status bar: Claude's star alone, in a square tinted
// like the run block in the color of the state. Its animation says the state
// without the color: the branches light up in turn while Claude works, the
// star breathes when Claude needs the user, it twinkles when a response
// arrives then shines still, and stays dim at rest. The state's name is in
// the tooltip.
class ClaudeIndicator : public QAbstractButton {
    Q_OBJECT

public:
    enum class Status {
        Ready,    // nothing in progress, nothing new
        Working,  // a response is in progress
        NeedsYou, // Claude waits for an approval or an answer
        Done,     // a response is ready, not seen yet
    };

    explicit ClaudeIndicator(QWidget *parent = nullptr);

    Status status() const { return status_; }

    void setStatus(Status status);
    void applyTheme(const Theme &theme);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void updateAnimation();
    void paintBackground(QPainter &painter, const QColor &color) const;
    void paintStar(QPainter &painter, const QRectF &box, const QColor &color) const;
    qreal branchOpacity(int branch) const;
    void advanceFrame();

    Status status_ = Status::Ready;
    Theme theme_;
    HoverProgress hover_{this};
    int frame_ = 0; // the animation's step since the state changed
    QTimer animationTimer_;
};

// The color of `status` in `theme`: amber (the theme's warning) while Claude
// works, the theme's accent when it needs the user, green when a response
// is ready, the muted text at rest. Also the dot on Claude's tabs.
QColor claudeStatusColor(ClaudeIndicator::Status status, const Theme &theme);
// "Working", "Needs you"…, as the indicator writes it.
QString claudeStatusText(ClaudeIndicator::Status status);
