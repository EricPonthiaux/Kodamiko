// SPDX-License-Identifier: GPL-3.0-only

#include "ConflictNavigator.h"

#include "ConflictSteps.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

#include <algorithm>

ConflictNavigator::ConflictNavigator(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , previous_(new QToolButton(this))
    , where_(new QLabel(this))
    , next_(new QToolButton(this))
    , steps_(new ConflictSteps(this))
    , nextOpen_(new QToolButton(this))
{
    previous_->setToolTip(tr("Previous conflict (Shift+F7)"));
    next_->setToolTip(tr("Next conflict (F7)"));
    nextOpen_->setToolTip(tr("Next conflict still to resolve (Alt+F7)"));
    for (QToolButton *button : {previous_, next_, nextOpen_}) {
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        UiMetrics::setScaledIconSize(button);
    }
    where_->setTextFormat(Qt::RichText);
    connect(previous_, &QToolButton::clicked, this, &ConflictNavigator::previousRequested);
    connect(next_, &QToolButton::clicked, this, &ConflictNavigator::nextRequested);
    connect(nextOpen_, &QToolButton::clicked, this, &ConflictNavigator::nextOpenRequested);
    connect(steps_, &ConflictSteps::conflictRequested, this, &ConflictNavigator::conflictRequested);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(previous_);
    layout->addWidget(where_);
    layout->addWidget(next_);
    layout->addSpacing(6);
    layout->addWidget(steps_);
    layout->addSpacing(4);
    layout->addWidget(nextOpen_);
    applyTheme(theme);
    setConflicts({}, -1);
}

void ConflictNavigator::setConflicts(const QList<ConflictState> &states, int current)
{
    const bool hasCurrent = current >= 0 && current < states.size();
    setVisible(!states.isEmpty());
    previous_->setEnabled(states.size() > 1);
    next_->setEnabled(states.size() > 1);
    where_->setText(hasCurrent ? tr("Conflict <b>%1</b> of %2").arg(current + 1).arg(states.size())
                               : tr("%n conflict(s)", nullptr, int(states.size())));
    steps_->setConflicts(states, current);
    const auto open =
        std::ranges::count_if(states, [](ConflictState state) { return state != ConflictState::Resolved; });
    nextOpen_->setText(open == 0 ? tr("All resolved") : tr("%n open", nullptr, int(open)));
    nextOpen_->setEnabled(open > 0);
    nextOpen_->setToolButtonStyle(open == 0 ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonTextOnly);
}

void ConflictNavigator::applyTheme(const Theme &theme)
{
    previous_->setIcon(UiIcons::icon(QStringLiteral("up"), theme.foreground));
    next_->setIcon(UiIcons::icon(QStringLiteral("down"), theme.foreground));
    nextOpen_->setIcon(UiIcons::icon(QStringLiteral("check"), ConflictStates::color(ConflictState::Resolved, theme)));
    steps_->setTheme(theme);
}
