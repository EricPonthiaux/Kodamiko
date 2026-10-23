// SPDX-License-Identifier: GPL-3.0-only

#include "ToolTipPlacement.h"

#include <QApplication>
#include <QHelpEvent>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#include <algorithm>

namespace {

// Qt's own gap between the mouse and the tooltip's left edge.
constexpr int mouseGapPx = 2;

bool isToolTip(const QObject *object)
{
    return object->isWidgetType() && static_cast<const QWidget *>(object)->windowType() == Qt::ToolTip;
}

class ToolTipFilter : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        // Every event of the application passes here: the cheap test first.
        const QEvent::Type type = event->type();
        if (type == QEvent::ToolTip) {
            rememberMouse(static_cast<QHelpEvent *>(event)->globalPos());
            return false;
        }
        const bool isWatchedType =
            type == QEvent::Show || type == QEvent::Move || type == QEvent::Resize || type == QEvent::Hide;
        if (!isWatchedType || !isToolTip(watched))
            return false;
        auto *tip = static_cast<QWidget *>(watched);
        if (type == QEvent::Hide) {
            if (tip == placedTip_)
                placedTip_ = nullptr;
            return false;
        }
        // A tooltip the mouse brought up, or the same one updated in place
        // (the file tree adds the last commit to it once known) or resized
        // once polished.
        if (isMouseFresh_ || tip == placedTip_) {
            placedTip_ = tip;
            placeBesideMouse(tip);
        }
        return false;
    }

private:
    // A help event that shows no tooltip must not place the next one, which
    // code may show anywhere: the mouse is fresh until the event is handled.
    void rememberMouse(QPoint globalPosition)
    {
        mouse_ = globalPosition;
        isMouseFresh_ = true;
        QTimer::singleShot(0, this, [this] { isMouseFresh_ = false; });
    }

    // Qt chose the side above or below the mouse; only the horizontal flip
    // is undone. Moving to the same position sends no new Move event.
    void placeBesideMouse(QWidget *tip) const
    {
        const QScreen *screen = QGuiApplication::screenAt(mouse_);
        const QRect area = (screen ? screen : tip->screen())->geometry();
        const int rightmostX = std::max(area.left(), area.right() + 1 - tip->frameGeometry().width());
        const int x = std::clamp(mouse_.x() + mouseGapPx, area.left(), rightmostX);
        tip->move(x, tip->y());
    }

    QPoint mouse_;
    bool isMouseFresh_ = false;
    QPointer<QWidget> placedTip_;
};

} // namespace

namespace ToolTipPlacement {

void install()
{
    Q_ASSERT(qApp);
    if (qApp->findChild<QObject *>(QStringLiteral("toolTipPlacement")))
        return;
    auto *filter = new ToolTipFilter(qApp);
    filter->setObjectName(QStringLiteral("toolTipPlacement"));
    qApp->installEventFilter(filter);
}

} // namespace ToolTipPlacement
