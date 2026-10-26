// SPDX-License-Identifier: GPL-3.0-only

#include "BusyIndicator.h"

#include "Spinner.h"

#include <QHBoxLayout>
#include <QLabel>

namespace {

constexpr int spinnerDiameterPx = 14;
constexpr int marginPx = 8;
constexpr int spacingPx = 6;

} // namespace

BusyIndicator::BusyIndicator(QWidget *parent)
    : QWidget(parent)
    , spinner_(new Spinner(this, spinnerDiameterPx))
    , label_(new QLabel(this))
{
    label_->setTextFormat(Qt::PlainText);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(marginPx, 0, marginPx, 0);
    layout->setSpacing(spacingPx);
    layout->addWidget(spinner_);
    layout->addWidget(label_);
    spinner_->setSpinning(false);
    hide();
}

void BusyIndicator::start(const QString &description)
{
    label_->setText(description);
    setToolTip(description);
    spinner_->setSpinning(true);
    show();
}

void BusyIndicator::finish()
{
    spinner_->setSpinning(false);
    hide();
}
