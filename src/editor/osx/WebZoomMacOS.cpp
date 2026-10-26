// SPDX-License-Identifier: GPL-3.0-only

#include "editor/WebZoom.h"

#include <QChildEvent>
#include <QWebEngineView>
#include <QWheelEvent>

#include <algorithm>

namespace {

// The bounds of QWebEnginePage::setZoomFactor.
constexpr qreal minimumZoom = 0.25;
constexpr qreal maximumZoom = 5.0;
constexpr qreal zoomStep = 1.1;

// Chromium zooms on Ctrl+wheel elsewhere, but on macOS only on a pinch:
// Command+wheel, Qt's Ctrl, is left to the page. The wheel reaches the
// widget the engine creates inside the view, not the view itself: the
// filter follows the view's children as they come. Owned by the view.
class WheelZoom : public QObject {
public:
    explicit WheelZoom(QWebEngineView *view)
        : QObject(view)
        , view_(view)
    {
        view->installEventFilter(this);
        for (QObject *child : view->children())
            watch(child);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == view_ && event->type() == QEvent::ChildAdded)
            watch(static_cast<QChildEvent *>(event)->child());
        if (event->type() != QEvent::Wheel)
            return QObject::eventFilter(watched, event);
        const auto *wheel = static_cast<QWheelEvent *>(event);
        const int delta = wheel->angleDelta().y();
        const bool isZoom = wheel->modifiers() & Qt::ControlModifier;
        if (!isZoom || delta == 0)
            return QObject::eventFilter(watched, event);
        const qreal factor = delta > 0 ? view_->zoomFactor() * zoomStep : view_->zoomFactor() / zoomStep;
        view_->setZoomFactor(std::clamp(factor, minimumZoom, maximumZoom));
        return true; // not a scroll
    }

private:
    // The engine's widget comes with the first page loaded, and again when
    // the render process restarts.
    void watch(QObject *child)
    {
        if (child->isWidgetType())
            child->installEventFilter(this);
    }

    QWebEngineView *view_;
};

} // namespace

void WebZoom::attach(QWebEngineView *view)
{
    new WheelZoom(view);
}
