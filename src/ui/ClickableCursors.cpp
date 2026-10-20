// SPDX-License-Identifier: GPL-3.0-only

#include "ClickableCursors.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QMouseEvent>
#include <QTabBar>

namespace {

// Marks the widgets whose cursor this filter manages, so that a cursor set
// by the code itself is never replaced.
constexpr const char *managedProperty = "duetClickableCursor";

bool isClickable(const QWidget *widget)
{
    return qobject_cast<const QAbstractButton *>(widget) || qobject_cast<const QComboBox *>(widget);
}

// The hand while the widget is enabled; the inherited cursor otherwise.
void showHandWhileEnabled(QWidget *widget)
{
    if (widget->isEnabled())
        widget->setCursor(Qt::PointingHandCursor);
    else
        widget->unsetCursor();
}

class ClickableCursorFilter : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        // Every event of the application passes here: the cheap test first.
        const QEvent::Type type = event->type();
        const bool isWatchedType = type == QEvent::Polish || type == QEvent::EnabledChange || type == QEvent::MouseMove;
        auto *widget = isWatchedType && watched->isWidgetType() ? static_cast<QWidget *>(watched) : nullptr;
        if (!widget)
            return false;
        switch (type) {
        case QEvent::Polish:
            adopt(widget);
            break;
        case QEvent::EnabledChange:
            // A tab bar's hand follows the mouse, which a disabled one
            // no longer receives.
            if (isManaged(widget) && isClickable(widget))
                showHandWhileEnabled(widget);
            else if (isManaged(widget))
                widget->unsetCursor();
            break;
        case QEvent::MouseMove:
            if (isManaged(widget))
                followTabs(widget, static_cast<QMouseEvent *>(event));
            break;
        default:
            break;
        }
        return false;
    }

private:
    static bool isManaged(const QWidget *widget) { return widget->property(managedProperty).toBool(); }

    static void adopt(QWidget *widget)
    {
        const bool isTabBar = qobject_cast<QTabBar *>(widget) != nullptr;
        if (isManaged(widget) || widget->testAttribute(Qt::WA_SetCursor) || !(isTabBar || isClickable(widget)))
            return;
        widget->setProperty(managedProperty, true);
        if (isTabBar)
            widget->setMouseTracking(true); // the hand only over a tab
        else
            showHandWhileEnabled(widget);
    }

    static void followTabs(QWidget *widget, const QMouseEvent *mouse)
    {
        auto *tabBar = qobject_cast<QTabBar *>(widget);
        if (!tabBar)
            return;
        const int tab = tabBar->tabAt(mouse->position().toPoint());
        if (tab >= 0 && tabBar->isTabEnabled(tab))
            tabBar->setCursor(Qt::PointingHandCursor);
        else
            tabBar->unsetCursor();
    }
};

} // namespace

namespace ClickableCursors {

void install()
{
    Q_ASSERT(qApp);
    // Owned by the application.
    qApp->installEventFilter(new ClickableCursorFilter(qApp));
}

} // namespace ClickableCursors
