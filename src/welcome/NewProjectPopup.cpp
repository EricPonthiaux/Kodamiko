// SPDX-License-Identifier: GPL-3.0-only

#include "NewProjectPopup.h"

#include "NewProjectForm.h"
#include "ui/PopupWidgets.h"

#include <QKeyEvent>
#include <QVBoxLayout>

NewProjectPopup::NewProjectPopup(const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("newProject"), QSizeF(0.45, 0.6), QSize(620, 440), parent)
    , form_(new NewProjectForm(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->addWidget(form_);
    connect(form_, &NewProjectForm::cancelled, this, &OverlayPopup::dismiss);
    connect(form_, &NewProjectForm::created, this, [this](const QString &folder) {
        dismiss();
        emit created(folder);
    });
    applyTheme(theme);
}

void NewProjectPopup::applyTheme(const Theme &theme)
{
    setStyleSheet(popupStyleSheet(theme));
    form_->applyTheme(theme);
}

void NewProjectPopup::popup()
{
    showCentered();
    form_->reset(); // after showing it: the name takes the focus
}

void NewProjectPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}
