#include "ViewHeader.h"

#include "AppFonts.h"
#include "UiIcons.h"
#include "UiMetrics.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

namespace {

// The Files pane's buttons.
constexpr QSize buttonSize(26, 24);
constexpr int iconSize = 16;

} // namespace

ViewHeader::ViewHeader(const QString &title, QWidget *parent)
    : QFrame(parent)
    , layout_(new QHBoxLayout(this))
    , detail_(new QLabel(this))
{
    setObjectName(QStringLiteral("viewHeader"));
    UiMetrics::setScaledHeight(this, UiMetrics::panelHeaderHeight);
    auto *titleLabel = new QLabel(title, this);
    AppFonts::setInterfaceFont(titleLabel, 1);
    detail_->setEnabled(false);
    detail_->hide();

    layout_->setContentsMargins(10, 0, 6, 0);
    layout_->setSpacing(2);
    layout_->addWidget(titleLabel);
    layout_->addSpacing(8);
    layout_->addWidget(detail_);
    layout_->addStretch(1);
}

QToolButton *ViewHeader::addButton(const QString &iconName, const QString &toolTip)
{
    auto *button = new QToolButton(this);
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIconSize(QSize(iconSize, iconSize));
    button->setFixedSize(buttonSize);
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
