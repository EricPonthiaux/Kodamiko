#include "SidePanel.h"

#include <QEvent>
#include <QImage>
#include <QPainter>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QTabBar>
#include <QVBoxLayout>

// The tabs' backgrounds come from the style sheet; their labels are drawn
// level into an image, then turned a quarter. Text drawn turned is not
// hinted, hence blurred; an image turned a quarter moves whole pixels.
class SideTabBar : public QTabBar {
public:
    using QTabBar::QTabBar;

    void applyTheme(const Theme &theme)
    {
        theme_ = theme;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this);
        const int hovered = underMouse() ? tabAt(mapFromGlobal(QCursor::pos())) : -1;
        for (int i = 0; i < count(); ++i) {
            QStyleOptionTab option;
            initStyleOption(&option, i);
            painter.drawControl(QStyle::CE_TabBarTabShape, option);
            drawLabel(painter, option.rect, i, labelColor(i, hovered));
        }
    }

    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::HoverMove || event->type() == QEvent::HoverLeave)
            update();
        return QTabBar::event(event);
    }

private:
    QColor labelColor(int index, int hovered) const
    {
        if (index == currentIndex())
            return theme_.accentText;
        if (index == hovered)
            return theme_.foreground;
        return theme_.mutedText;
    }

    void drawLabel(QPainter &painter, const QRect &rect, int index, const QColor &color)
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

    Theme theme_;
};

SidePanel::SidePanel(QWidget *parent)
    : QStackedWidget(parent)
    , strip_(new QWidget(parent))
    , tabBar_(new SideTabBar(strip_))
{
    strip_->setObjectName(QStringLiteral("sideStrip"));
    strip_->setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(strip_);
    layout->setContentsMargins(0, 4, 0, 4);
    layout->setSpacing(0);
    layout->addWidget(tabBar_);
    layout->addStretch(1);
    tabBar_->setObjectName(QStringLiteral("sideTabs"));
    tabBar_->setShape(QTabBar::RoundedWest);
    tabBar_->setDocumentMode(true);
    tabBar_->setDrawBase(false);
    tabBar_->setExpanding(false);
    tabBar_->setUsesScrollButtons(false);
    tabBar_->setFocusPolicy(Qt::NoFocus);
    tabBar_->setAttribute(Qt::WA_Hover);
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
    addWidget(view);
    const QSignalBlocker blocker(tabBar_); // no unfolding while being built
    return tabBar_->addTab(icon, title);
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
    setVisible(visible);
    emit panelVisibilityChanged(visible);
}
