// SPDX-License-Identifier: GPL-3.0-only

#include "WorkspaceStatusBar.h"

#include "AppFonts.h"
#include "ElidingToolButton.h"
#include "UiIcons.h"
#include "UiMetrics.h"

#include <QBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QScreen>
#include <QTabBar>
#include <QToolButton>

#include <algorithm>

namespace {

constexpr int boxGapPx = 4;
// Between the mode track and the window's right edge, which it touched.
constexpr int trailingMarginPx = 8;

class StatusRow : public QWidget {
public:
    using QWidget::QWidget;

    // Expanded details must not impose a minimum width on the window: the
    // bar needs to receive the resize before moving them into the popup.
    QSize minimumSizeHint() const override { return {0, QWidget::minimumSizeHint().height()}; }
};

} // namespace

WorkspaceStatusBar::WorkspaceStatusBar(QTabBar *modes, QWidget *parent)
    : QStatusBar(parent)
    , modes_(modes)
{
    setSizeGripEnabled(false);
    trailing_ = new StatusRow(this);
    trailing_->setObjectName(QStringLiteral("statusTrailing"));
    trailingLayout_ = new QHBoxLayout(trailing_);
    trailingLayout_->setContentsMargins(0, 0, 0, 0);
    details_ = new QWidget(trailing_);
    details_->setObjectName(QStringLiteral("statusDetails"));
    detailsLayout_ = new QHBoxLayout(details_);
    detailsLayout_->setContentsMargins(0, 0, 0, 0);
    trailingLayout_->addWidget(details_);
    more_ = new QToolButton(trailing_);
    more_->setObjectName(QStringLiteral("statusOverflow"));
    more_->setAutoRaise(true);
    more_->setToolTip(tr("Workspace Status"));
    more_->setAccessibleName(tr("Workspace Status"));
    more_->hide();
    connect(more_, &QToolButton::clicked, this, &WorkspaceStatusBar::showDetails);
    trailingLayout_->addWidget(more_);
    trailingLayout_->addWidget(modes_);
    trailing_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    addPermanentWidget(trailing_);

    popup_ = new QFrame(this, Qt::Popup);
    popup_->setObjectName(QStringLiteral("statusPopup"));
    // Its corners rounded on nothing, as a menu's.
    popup_->setAttribute(Qt::WA_TranslucentBackground);
    auto *popupLayout = new QVBoxLayout(popup_);
    popupLayout->setContentsMargins(12, 12, 12, 12);
    popupLayout->setSizeConstraint(QLayout::SetFixedSize);
    for (int i = 0; i < modes_->count(); ++i)
        modeLabels_.append(modes_->tabText(i));
    connect(modes_, &QTabBar::currentChanged, this, &WorkspaceStatusBar::updateModeLabels);
    connect(this, &QStatusBar::messageChanged, this, &WorkspaceStatusBar::scheduleLayout);
    trailing_->installEventFilter(this);
    modes_->installEventFilter(this);
    AppFonts::followApplicationFont(this, [this] {
        const int control = UiMetrics::controlHeight();
        more_->setFixedSize(control, control);
        more_->setIconSize(UiMetrics::scaledSquare(UiMetrics::iconSize));
        modes_->setIconSize(UiMetrics::scaledSquare(UiMetrics::iconSize));
        trailingLayout_->setSpacing(UiMetrics::scaled(8));
        // Room above and below the boxes: they stand in the middle of the
        // bar, clear of its line.
        const int gap = UiMetrics::scaled(boxGapPx);
        trailingLayout_->setContentsMargins(0, gap, UiMetrics::scaled(trailingMarginPx), gap);
        detailsLayout_->setSpacing(UiMetrics::scaled(12));
        scheduleLayout();
    });
}

void WorkspaceStatusBar::addGroup(const QString &title, const QList<QWidget *> &widgets)
{
    auto *panel = new QWidget(details_);
    panel->setObjectName(QStringLiteral("statusGroup"));
    auto *column = new QVBoxLayout(panel);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(4);
    auto *heading = new QLabel(title, panel);
    heading->setObjectName(QStringLiteral("statusHeading"));
    heading->hide();
    column->addWidget(heading);
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    column->addLayout(row);
    for (QWidget *widget : widgets) {
        const bool hidden = widget->isHidden();
        row->addWidget(widget);
        widget->setVisible(!hidden);
        widget->installEventFilter(this);
        if (auto *label = qobject_cast<QLabel *>(widget)) {
            AppFonts::followApplicationFont(label, [label] { label->setMaximumWidth(UiMetrics::scaled(320)); });
        }
    }
    panel->installEventFilter(this);
    detailsLayout_->addWidget(panel);
    groups_.append({panel, heading, row, widgets});
    scheduleLayout();
}

void WorkspaceStatusBar::scheduleLayout()
{
    if (layoutScheduled_)
        return;
    layoutScheduled_ = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            layoutScheduled_ = false;
            updateResponsiveLayout();
        },
        Qt::QueuedConnection);
}

int WorkspaceStatusBar::boundedWidth(const Group &group)
{
    // The labels' growth into spare room must not count, or the bar would
    // keep the room it gave them.
    int width = group.row->sizeHint().width();
    for (const ElidingToolButton *button : group.panel->findChildren<ElidingToolButton *>()) {
        if (button->isVisibleTo(group.panel))
            width -= button->sizeHint().width() - button->boundedWidth();
    }
    return width;
}

int WorkspaceStatusBar::expandedModesWidth() const
{
    int width = 0;
    for (int i = 0; i < modes_->count(); ++i) {
        if (!modes_->isTabVisible(i))
            continue;
        // Keep the style's actual icon, padding and margin widths, even
        // when the inactive labels have been removed for the compact bar.
        width += modes_->tabRect(i).width() + modes_->fontMetrics().horizontalAdvance(modeLabels_.at(i)) -
                 modes_->fontMetrics().horizontalAdvance(modes_->tabText(i));
    }
    return width;
}

void WorkspaceStatusBar::updateResponsiveLayout()
{
    const int reserve = currentMessage().isEmpty() ? UiMetrics::scaled(48) : qMin(width() / 3, UiMetrics::scaled(280));
    const int outerWidth = qMax(0, width() - reserve - 16);
    trailing_->setMaximumWidth(outerWidth);
    // What the boxes may take, inside the margin that keeps them off the edge.
    const QMargins margins = trailingLayout_->contentsMargins();
    const int available = qMax(0, outerWidth - margins.left() - margins.right());
    const int gap = trailingLayout_->spacing();
    int detailWidth = 0;
    for (const Group &group : std::as_const(groups_)) {
        const bool visible =
            std::ranges::any_of(group.widgets, [](const QWidget *widget) { return !widget->isHidden(); });
        group.panel->setVisible(visible);
        if (visible)
            detailWidth += boundedWidth(group) + detailsLayout_->spacing();
    }
    const int modesWidth = expandedModesWidth();
    const bool overflow = detailWidth > 0 && detailWidth + modesWidth + gap > available;
    more_->setVisible(overflow);
    const QIcon icon = UiIcons::icon(QStringLiteral("more"), palette().color(QPalette::ButtonText));
    if (more_->icon().cacheKey() != icon.cacheKey())
        more_->setIcon(icon);
    compactModes_ = modesWidth + (overflow ? more_->width() + gap : 0) > available;
    updateModeLabels();
    const int tabWidth = compactModes_ ? modes_->sizeHint().width() : modesWidth;
    const int spare = arrangeGroups(available - tabWidth - gap - (overflow ? more_->width() + gap : 0));
    widenLabels(spare);
    trailing_->updateGeometry();
}

int WorkspaceStatusBar::arrangeGroups(int available)
{
    // Retain the groups nearest the modes first: Git, then editor details.
    // Lower priority groups remain live in the popup, including their menus.
    QList<bool> overflow(groups_.size(), false);
    for (qsizetype i = groups_.size(); i-- > 0;) {
        const Group &group = groups_.at(i);
        if (group.panel->isHidden())
            continue;
        const int width = boundedWidth(group) + detailsLayout_->spacing();
        overflow[i] = width > available;
        if (!overflow.at(i))
            available -= width;
    }
    int inlineCount = 0;
    int popupCount = 0;
    for (qsizetype i = 0; i < groups_.size(); ++i) {
        Group &group = groups_[i];
        moveGroup(group, overflow.at(i), overflow.at(i) ? popupCount : inlineCount);
        if (group.panel->isHidden())
            continue;
        if (overflow.at(i))
            ++popupCount;
        else
            ++inlineCount;
    }
    details_->setVisible(inlineCount > 0);
    more_->setVisible(popupCount > 0);
    if (popupCount == 0)
        popup_->hide();
    return qMax(0, available);
}

void WorkspaceStatusBar::widenLabels(int spare)
{
    // The groups nearest the modes lengthen first, as they stay longest.
    for (qsizetype i = groups_.size(); i-- > 0;) {
        const Group &group = groups_.at(i);
        for (ElidingToolButton *button : group.panel->findChildren<ElidingToolButton *>())
            spare -= button->widenInto(group.inPopup ? 0 : spare);
    }
}

void WorkspaceStatusBar::moveGroup(Group &group, bool intoPopup, int index)
{
    if (group.inPopup == intoPopup)
        return;
    const bool visible = !group.panel->isHidden();
    group.inPopup = intoPopup;
    if (intoPopup) {
        detailsLayout_->removeWidget(group.panel);
        static_cast<QBoxLayout *>(popup_->layout())->insertWidget(index, group.panel);
    } else {
        popup_->layout()->removeWidget(group.panel);
        detailsLayout_->insertWidget(index, group.panel);
    }
    group.heading->setVisible(intoPopup);
    group.panel->setVisible(visible);
}

void WorkspaceStatusBar::updateModeLabels()
{
    for (int i = 0; i < modes_->count(); ++i) {
        const QString label = compactModes_ && i != modes_->currentIndex() ? QString() : modeLabels_.at(i);
        if (modes_->tabText(i) != label)
            modes_->setTabText(i, label);
        modes_->setAccessibleTabName(i, modeLabels_.at(i));
    }
}

void WorkspaceStatusBar::showDetails()
{
    popup_->adjustSize();
    const QRect screen = more_->screen()->availableGeometry();
    QPoint position = more_->mapToGlobal(QPoint(more_->width() - popup_->width(), -popup_->height() - 6));
    position.setX(qBound(screen.left(), position.x(), qMax(screen.left(), screen.right() - popup_->width() + 1)));
    position.setY(qMax(screen.top(), position.y()));
    popup_->move(position);
    popup_->show();
}

bool WorkspaceStatusBar::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::LayoutRequest:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::FontChange:
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
        scheduleLayout();
        break;
    default:
        break;
    }
    return QStatusBar::eventFilter(watched, event);
}

void WorkspaceStatusBar::resizeEvent(QResizeEvent *event)
{
    QStatusBar::resizeEvent(event);
    scheduleLayout();
}
