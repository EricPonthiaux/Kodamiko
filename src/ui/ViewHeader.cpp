// SPDX-License-Identifier: GPL-3.0-only

#include "ViewHeader.h"

#include "AppFonts.h"
#include "UiIcons.h"
#include "UiMetrics.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// The Files pane's buttons.
constexpr QSize buttonSize(26, 24);

} // namespace

ViewHeader::ViewHeader(const QString &title, QWidget *parent)
    : ViewHeader(new QLabel(title), parent)
{
}

ViewHeader::ViewHeader(QWidget *title, QWidget *parent)
    : QFrame(parent)
    , detail_(new QLabel(this))
{
    setObjectName(QStringLiteral("viewHeader"));
    AppFonts::followApplicationFont(this, [this] { updateHeight(); });
    auto *tab = new QFrame(this);
    tab->setObjectName(QStringLiteral("viewHeaderTab"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(tab);
    layout_ = new QHBoxLayout(tab);
    AppFonts::setInterfaceFont(title, 1);
    detail_->setEnabled(false);
    detail_->hide();

    // Text starts at the same position as the Files selector's text.
    layout_->setContentsMargins(qobject_cast<QLabel *>(title) ? 12 : 6, 0, 6, 1);
    layout_->setSpacing(2);
    layout_->addWidget(title);
    layout_->addSpacing(8);
    layout_->addWidget(detail_);
    layout_->addStretch(1);
}

void ViewHeader::alignWith(QTabBar *tabs)
{
    Q_ASSERT(tabs);
    if (editorTabBar_)
        editorTabBar_->removeEventFilter(this);
    editorTabBar_ = tabs;
    tabs->installEventFilter(this);
    updateHeight();
}

void ViewHeader::updateHeight()
{
    const int height = editorTabBar_ && editorTabBar_->count() > 0 ? editorTabBar_->sizeHint().height()
                                                                   : UiMetrics::scaled(UiMetrics::panelHeaderHeight);
    setFixedHeight(height);
}

bool ViewHeader::eventFilter(QObject *watched, QEvent *event)
{
    const bool tabsChanged = event->type() == QEvent::Resize || event->type() == QEvent::Show ||
                             event->type() == QEvent::LayoutRequest || event->type() == QEvent::FontChange ||
                             event->type() == QEvent::StyleChange;
    if (watched == editorTabBar_ && tabsChanged)
        QMetaObject::invokeMethod(this, &ViewHeader::updateHeight, Qt::QueuedConnection);
    return QFrame::eventFilter(watched, event);
}

QToolButton *ViewHeader::addButton(const QString &iconName, const QString &toolTip)
{
    auto *button = new QToolButton(this);
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    UiMetrics::setScaledIconSize(button);
    UiMetrics::setScaledSize(button, buttonSize);
    layout_->addWidget(button);
    buttons_.append({button, iconName});
    return button;
}

void ViewHeader::addWidget(QWidget *widget)
{
    layout_->addWidget(widget);
}

QToolButton *ViewHeader::addTextButton(const QString &iconName, const QString &text, const QString &toolTip)
{
    auto *button = new QToolButton(this);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    UiMetrics::setScaledIconSize(button);
    layout_->addWidget(button);
    buttons_.append({button, iconName});
    return button;
}

void ViewHeader::setDetail(const QString &text)
{
    detail_->setText(text);
    detail_->setVisible(!text.isEmpty());
}

void ViewHeader::setIconColor(const QColor &color)
{
    for (const auto &[button, iconName] : std::as_const(buttons_))
        button->setIcon(UiIcons::icon(iconName, color));
}
