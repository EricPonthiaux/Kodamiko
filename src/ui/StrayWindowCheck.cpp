// SPDX-License-Identifier: GPL-3.0-only

#include "StrayWindowCheck.h"

#include <QApplication>
#include <QEvent>
#include <QWidget>

namespace {

constexpr const char *allowedProperty = "duetTopLevel";

bool isStrayWindow(const QWidget *widget)
{
    return widget->isWindow() && widget->windowType() == Qt::Window && !widget->property(allowedProperty).toBool();
}

class StrayWindowFilter : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        // Every event of the application passes here: the cheap test first.
        if (event->type() != QEvent::Show || !watched->isWidgetType())
            return false;
        const auto *widget = static_cast<QWidget *>(watched);
        if (isStrayWindow(widget))
            qWarning("A widget was shown as a window of its own (%s \"%s\"): give it a parent before showing it",
                     widget->metaObject()->className(), qPrintable(widget->objectName()));
        return false;
    }
};

} // namespace

void StrayWindowCheck::install()
{
    qApp->installEventFilter(new StrayWindowFilter(qApp));
}

void StrayWindowCheck::allow(QWidget *window)
{
    window->setProperty(allowedProperty, true);
}
