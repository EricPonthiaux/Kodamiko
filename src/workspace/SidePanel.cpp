// SPDX-License-Identifier: GPL-3.0-only

#include "SidePanel.h"

#include "ui/Motion.h"
#include "ui/UiMetrics.h"
#include "ui/ViewHeader.h"

#include <QEvent>
#include <QImage>
#include <QPainter>
#include <QSplitter>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QTabBar>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// The panel's width when it comes back after being dragged down to nothing.
constexpr int unfoldedWidthPx = 260;

} // namespace

// The tabs' backgrounds come from the style sheet; their labels are drawn
// level into an image, then turned a quarter. Text drawn turned is not
// hinted, hence blurred; an image turned a quarter moves whole pixels.
class SideTabBar : public QTabBar {
public:
    SideTabBar(const SidePanel *panel, QWidget *parent)
        : QTabBar(parent)
        , panel_(panel)
    {
        connect(panel, &SidePanel::panelVisibilityChanged, this, qOverload<>(&QWidget::update));
    }

    void applyTheme(const Theme &theme)
    {
        theme_ = theme;
        updateGeometry();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this);
        const int hovered = underMouse() ? tabAt(mapFromGlobal(QCursor::pos())) : -1;
        const int open = panel_->isPanelVisible() ? currentIndex() : -1;
        for (int i = 0; i < count(); ++i) {
            if (i != open)
                drawTab(painter, i, hovered);
        }
        drawSeparator(painter, open);
        if (open >= 0)
            drawTab(painter, open, hovered);
    }

    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::HoverMove || event->type() == QEvent::HoverLeave)
            update();
        return QTabBar::event(event);
    }

private:
    // The line between the strip and the panel, open beside the open tab,
    // whose background reaches the panel's. Hovered tabs keep it. A filled
    // column rather than a line: at a fractional scale, a line falls between
    // device pixels and leaves the last one bare.
    void drawSeparator(QPainter &painter, int open) const
    {
        const int x = width() - 1;
        if (open < 0) {
            painter.fillRect(QRect(x, 0, 1, height()), theme_.line);
            return;
        }
        const QRect tab = tabRect(open);
        painter.fillRect(QRect(x, 0, 1, tab.top()), theme_.line);
        painter.fillRect(QRect(QPoint(x, tab.bottom() + 1), QPoint(x, height() - 1)), theme_.line);
    }

    void drawTab(QStylePainter &painter, int index, int hovered) const
    {
        QStyleOptionTab option;
        initStyleOption(&option, index);
        const QRect labelRect = option.rect;
        // One column past the bar, whose edge clips it: it never paints over
        // the panel. Up to its own edge only, the tab's last device pixel is
        // half covered at a scale of 1.25 or 1.75, a line between it and the
        // panel.
        option.rect.setRight(width());
        if (!panel_->isPanelVisible())
            option.state &= ~QStyle::State_Selected;
        painter.drawControl(QStyle::CE_TabBarTabShape, option);
        drawLabel(painter, labelRect, index, labelColor(index, hovered));
    }

    QColor labelColor(int index, int hovered) const
    {
        if (index == currentIndex() && panel_->isPanelVisible())
            return theme_.foreground;
        if (index == hovered)
            return theme_.foreground;
        return theme_.mutedText;
    }

    void drawLabel(QPainter &painter, const QRect &rect, int index, const QColor &color) const
    {
        const qreal ratio = devicePixelRatioF();
        const QImage image = levelLabel(QSize(rect.height(), rect.width()), index, color);
        // Read from bottom to top.
        QImage turned = image.transformed(QTransform().rotate(270));
        turned.setDevicePixelRatio(ratio);
        painter.drawImage(rect.topLeft(), turned);
    }

    // The tab's icon and text, centered in an image of size `level`.
    QImage levelLabel(const QSize &level, int index, const QColor &color) const
    {
        const QIcon icon = tabIcon(index);
        const QString text = tabText(index);
        const qreal ratio = devicePixelRatioF();
        QImage image(level * ratio, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(ratio);
        image.fill(Qt::transparent);
        QPainter imagePainter(&image);
        imagePainter.setFont(font());
        imagePainter.setPen(color);
        const QSize icons = icon.isNull() ? QSize() : iconSize();
        const int spacing = icon.isNull() ? 0 : 4;
        const int textWidth = fontMetrics().horizontalAdvance(text);
        int x = (level.width() - icons.width() - spacing - textWidth) / 2;
        if (!icon.isNull()) {
            icon.paint(&imagePainter, QRect(QPoint(x, (level.height() - icons.height()) / 2), icons));
            x += icons.width() + spacing;
        }
        imagePainter.drawText(QRect(x, 0, textWidth, level.height()), Qt::AlignVCenter | Qt::TextShowMnemonic, text);
        imagePainter.end();
        return image;
    }

    const SidePanel *panel_;
    Theme theme_;
};

SidePanel::SidePanel(QWidget *parent)
    : QStackedWidget(parent)
    , strip_(new QWidget(parent))
    , tabBar_(new SideTabBar(this, strip_))
{
    strip_->setObjectName(QStringLiteral("sideStrip"));
    strip_->setAttribute(Qt::WA_StyledBackground);
    strip_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto *layout = new QVBoxLayout(strip_);
    // The bar paints the full-height separator; its selected tab reaches
    // the panel without a margin between their matching backgrounds.
    layout->setContentsMargins(0, UiMetrics::tabTopMargin, 0, 0);
    layout->setSpacing(0);
    tabBar_->setObjectName(QStringLiteral("sideTabs"));
    tabBar_->setShape(QTabBar::RoundedWest);
    tabBar_->setDocumentMode(true);
    tabBar_->setDrawBase(false);
    tabBar_->setExpanding(false);
    tabBar_->setUsesScrollButtons(true);
    // Shorter than its tabs on a short window, which then scroll.
    tabBar_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    tabBar_->setFocusPolicy(Qt::NoFocus);
    tabBar_->setAttribute(Qt::WA_Hover);
    tabBar_->ensurePolished();
    layout->addWidget(tabBar_, 1);
    connect(tabBar_, &QTabBar::currentChanged, this, [this](int index) {
        setCurrentIndex(index);
        setPanelVisible(true);
    });
    // The current tab, clicked again: folds or unfolds the panel.
    connect(tabBar_, &QTabBar::tabBarClicked, this, [this](int index) {
        if (index >= 0 && index == tabBar_->currentIndex())
            setPanelVisible(!isPanelVisible());
    });
}

int SidePanel::addView(QWidget *view, const QIcon &icon, const QString &title)
{
    view->setAttribute(Qt::WA_StyledBackground);
    addWidget(view);
    const QSignalBlocker blocker(tabBar_); // no unfolding while being built
    const int index = tabBar_->addTab(icon, title);
    tabBar_->updateGeometry();
    return index;
}

void SidePanel::alignHeadersWith(QTabBar *tabs)
{
    for (auto *header : findChildren<ViewHeader *>())
        header->alignWith(tabs);
}

QTabBar *SidePanel::tabBar() const
{
    return tabBar_;
}

void SidePanel::applyTheme(const Theme &theme)
{
    tabBar_->applyTheme(theme);
}

void SidePanel::setViewIcon(QWidget *view, const QIcon &icon)
{
    tabBar_->setTabIcon(indexOf(view), icon);
    // Hidden, QTabBar does not report that its icons lengthened it: the strip
    // would keep its old height, shorter than the tabs, and scroll them.
    tabBar_->updateGeometry();
}

void SidePanel::showView(QWidget *view)
{
    tabBar_->setCurrentIndex(indexOf(view));
    setCurrentWidget(view);
    setPanelVisible(true);
}

void SidePanel::setPanelVisible(bool visible)
{
    if (visible == isPanelVisible())
        return;
    // Folding is immediate: the editor takes the room back at once.
    if (visible)
        Motion::fadeIn(this);
    setVisible(visible);
    if (visible)
        restoreCollapsedWidth();
    emit panelVisibilityChanged(visible);
}

void SidePanel::foldWithSplitter(QSplitter *splitter)
{
    splitter_ = splitter;
    splitter->setCollapsible(splitter->indexOf(this), true);
    connect(splitter, &QSplitter::splitterMoved, this, [this] {
        if (splitter_->sizes().at(splitter_->indexOf(this)) == 0)
            setPanelVisible(false);
    });
}

// A panel dragged down to nothing comes back at a usable width, taken from
// the pane next to it.
void SidePanel::restoreCollapsedWidth()
{
    if (!splitter_)
        return;
    QList<int> sizes = splitter_->sizes();
    const int index = splitter_->indexOf(this);
    const int neighbor = index + 1 < sizes.size() ? index + 1 : index - 1;
    if (sizes.at(index) > 0 || neighbor < 0)
        return;
    const int width = std::min(unfoldedWidthPx, sizes.at(neighbor) / 2);
    sizes[index] = width;
    sizes[neighbor] -= width;
    splitter_->setSizes(sizes);
}
