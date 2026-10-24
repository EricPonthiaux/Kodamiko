// SPDX-License-Identifier: GPL-3.0-only

#include "PopupSearchField.h"

#include "ThemeStyle.h"
#include "UiIcons.h"

#include <QAction>
#include <QEvent>

PopupSearchField::PopupSearchField(QWidget *parent)
    : QLineEdit(parent)
    , magnifier_(addAction(QIcon(), QLineEdit::LeadingPosition))
{
    setObjectName(QStringLiteral("popupSearch"));
    setClearButtonEnabled(true);
    updateIcon();
}

void PopupSearchField::changeEvent(QEvent *event)
{
    // A new theme comes with a new palette.
    if (event->type() == QEvent::PaletteChange)
        updateIcon();
    QLineEdit::changeEvent(event);
}

void PopupSearchField::updateIcon()
{
    magnifier_->setIcon(UiIcons::icon(QStringLiteral("search"), ThemeStyle::applied().mutedText));
}
