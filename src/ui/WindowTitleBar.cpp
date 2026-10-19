#include "WindowTitleBar.h"

#include "AppFonts.h"
#include "BrandMark.h"
#include "UiMetrics.h"
#include "WindowControls.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

WindowTitleBar::WindowTitleBar(QWidget *parent)
    : QWidget(parent)
    , theme_(Theme::dark())
    , icon_(new QLabel(this))
    , title_(new QLabel(this))
    , controls_(new WindowControls(this))
{
    // Clicks on the icon and the title go to the bar: they move the window.
    icon_->setAttribute(Qt::WA_TransparentForMouseEvents);
    title_->setAttribute(Qt::WA_TransparentForMouseEvents);
    AppFonts::setInterfaceFont(title_, 1, QFont::Medium);
    title_->setText(window()->windowTitle());

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(UiMetrics::scaled(10), 0, 0, 0);
    layout->setSpacing(UiMetrics::scaled(8));
    layout->addWidget(icon_);
    layout->addWidget(title_);
    layout->addStretch();
    layout->addWidget(controls_);
    AppFonts::followApplicationFont(this, [this] {
        setFixedHeight(UiMetrics::scaled(UiMetrics::titleBarHeight));
        updateIcon();
    });
    window()->installEventFilter(this); // its title
}

void WindowTitleBar::applyTheme(const Theme &theme)
{
    theme_ = theme;
    controls_->applyTheme(theme);
    update();
}

void WindowTitleBar::updateIcon()
{
    // The size of the main title bar's logo.
    const int size = UiMetrics::scaled(UiMetrics::toolbarButtonSize);
    icon_->setPixmap(BrandMark::appIcon(size, devicePixelRatioF()));
    icon_->setFixedSize(size, size);
}

bool WindowTitleBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == window() && event->type() == QEvent::WindowTitleChange)
        title_->setText(window()->windowTitle());
    return QWidget::eventFilter(watched, event);
}

void WindowTitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WindowTitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        WindowControls::toggleMaximized(window());
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void WindowTitleBar::paintEvent(QPaintEvent *)
{
    // A line under the bar, as under the main one; the background is the window's.
    QPainter painter(this);
    painter.setPen(theme_.line);
    painter.drawLine(QPointF(0, height() - 0.5), QPointF(width(), height() - 0.5));
}
