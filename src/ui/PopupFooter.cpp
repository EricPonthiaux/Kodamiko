// SPDX-License-Identifier: GPL-3.0-only

#include "PopupFooter.h"

#include "PopupMetrics.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

PopupFooter::PopupFooter(QWidget *parent)
    : QFrame(parent)
    , layout_(new QHBoxLayout(this))
    , hint_(new QLabel(this))
{
    setObjectName(QStringLiteral("popupFooter"));
    hint_->setObjectName(QStringLiteral("footerHint"));
    layout_->setContentsMargins(PopupMetrics::footerMargins);
    layout_->setSpacing(PopupMetrics::footerSpacing);
    layout_->addWidget(hint_, 1);
}

void PopupFooter::setHint(const QString &text)
{
    hint_->setText(text);
}

void PopupFooter::addButton(QPushButton *button)
{
    button->setParent(this);
    button->setMinimumWidth(PopupMetrics::buttonMinWidth);
    layout_->addWidget(button);
}

void PopupFooter::addLeadingButton(QPushButton *button)
{
    button->setParent(this);
    button->setMinimumWidth(PopupMetrics::buttonMinWidth);
    layout_->insertWidget(layout_->indexOf(hint_), button);
}
