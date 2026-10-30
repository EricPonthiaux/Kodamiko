// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "AgentMark.h"
#include "ui/HoverProgress.h"
#include "ui/Theme.h"

#include <QAbstractButton>
#include <QPointer>
#include <QTimer>

class AgentProvider;
class QTabBar;

// The agent's state in the status bar: the provider's mark alone (see
// AgentMark), in a square tinted like the run block in the color of the
// state. The mark's animation says the state without the color. The state's
// name is in the tooltip. Without a provider, the square stays empty.
class AgentIndicator : public QAbstractButton {
    Q_OBJECT

public:
    explicit AgentIndicator(QWidget *parent = nullptr);

    AgentStatus status() const { return status_; }

    void setStatus(AgentStatus status);
    // The agent shown: its name and mark. Not owned.
    void setProvider(const AgentProvider *provider);
    void applyTheme(const Theme &theme);
    // Centers the square under the side-tab column at the window's left
    // edge, following its width as the layout or interface font changes.
    // Not owned.
    void alignWith(QTabBar *column);

    QSize sizeHint() const override;
    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void paintEvent(QPaintEvent *event) override;
    // Only the square: the contents margins do not click.
    bool hitButton(const QPoint &pos) const override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updateAnimation();
    void paintBackground(QPainter &painter, const QColor &color) const;
    void updateAccessibleName();
    void advanceFrame();
    int squareSide() const;
    void updateAlignment();

    AgentStatus status_ = AgentStatus::Ready;
    const AgentProvider *provider_ = nullptr;
    Theme theme_;
    HoverProgress hover_{this};
    int frame_ = 0; // the animation's step since the state changed
    QTimer animationTimer_;
    QPointer<QTabBar> column_;
};

// The color of `status` in `theme`: amber (the theme's warning) while the agent
// works, the theme's accent when it needs the user, green when a response
// is ready, the muted text at rest. Also the dot on the agents' tabs.
QColor agentStatusColor(AgentStatus status, const Theme &theme);
// "Working", "Needs you"…, as the indicator writes it.
QString agentStatusText(AgentStatus status);
