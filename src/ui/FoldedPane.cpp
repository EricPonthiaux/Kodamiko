// SPDX-License-Identifier: GPL-3.0-only

#include "FoldedPane.h"

#include "UiIcons.h"

#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

constexpr int titleGap = 8; // between the button and the title
constexpr int margin = 4;

} // namespace

FoldedPane::FoldedPane(QString title, QString iconName, const QString &toolTip, QWidget *parent)
    : QFrame(parent)
    , title_(std::move(title))
    , iconName_(std::move(iconName))
    , expandButton_(new QToolButton(this))
{
    setObjectName(QStringLiteral("foldedPane"));
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    expandButton_->setAutoRaise(true);
    expandButton_->setFocusPolicy(Qt::NoFocus);
    expandButton_->setToolTip(toolTip);
    connect(expandButton_, &QToolButton::clicked, this, &FoldedPane::expandRequested);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->addWidget(expandButton_);
    layout->addStretch();
}

void FoldedPane::applyTheme(const Theme &theme)
{
    expandButton_->setIcon(UiIcons::icon(iconName_, theme.foreground));
}

void FoldedPane::setButtonRowHeight(int height)
{
    const int top = std::max(0, (height - expandButton_->sizeHint().height()) / 2);
    layout()->setContentsMargins(margin, top, margin, margin);
}

void FoldedPane::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);
    // Read top to bottom, its top under the button.
    QPainter painter(this);
    painter.setPen(palette().color(QPalette::WindowText));
    const QFontMetrics metrics(font());
    const int top = expandButton_->geometry().bottom() + titleGap;
    painter.translate(QRectF(expandButton_->geometry()).center().x() + metrics.height() / 2.0, top);
    painter.rotate(90);
    painter.drawText(QRect(0, 0, height() - top, metrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(title_, Qt::ElideRight, height() - top));
}
