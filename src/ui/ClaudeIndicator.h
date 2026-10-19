#pragma once

#include "Theme.h"

#include <QAbstractButton>
#include <QTimer>

// Claude's state in the title bar: [ ✳ Claude │ Working ], tinted like the
// run block in the color of the state, at the same width whatever the state
// so that the bar does not move. Its icon says the state without the color:
// a star whose branches light up in turn while Claude works, a bubble when
// it needs the user, a check when a response is ready, a dim star at rest.
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
    void paintBackground(QPainter &painter, const QColor &color) const;
    void paintIcon(QPainter &painter, const QRectF &box, const QColor &color) const;
    void paintStar(QPainter &painter, const QRectF &box, const QColor &color) const;
    void advanceLitBranch();

    Status status_ = Status::Ready;
    Theme theme_;
    bool hovered_ = false;
    int litBranch_ = 0; // the brightest branch of the star, while working
    QTimer sparkTimer_;
};

// The color of `status` in `theme`: amber (the theme's warning) while Claude
// works, the theme's accent when it needs the user, green when a response
// is ready, the muted text at rest. Also the dot on Claude's tabs.
QColor claudeStatusColor(ClaudeIndicator::Status status, const Theme &theme);
// "Working", "Needs you"…, as the indicator writes it.
QString claudeStatusText(ClaudeIndicator::Status status);
