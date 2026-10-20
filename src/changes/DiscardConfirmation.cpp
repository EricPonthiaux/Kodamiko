// SPDX-License-Identifier: GPL-3.0-only

#include "DiscardConfirmation.h"

#include <QMessageBox>
#include <QPushButton>

namespace DiscardConfirmation {

QString hint()
{
    return QObject::tr("A copy is kept: Git menu ▸ Restore a Snapshot.");
}

// "Discard" rather than "Cancel", which would be confused with the Cancel button.
bool ask(QWidget *parent, const QString &question, const QString &details)
{
    QMessageBox box(QMessageBox::Warning, QObject::tr("Discard Changes"), question, QMessageBox::NoButton, parent);
    box.setInformativeText(details.isEmpty() ? hint() : details + QLatin1Char(' ') + hint());
    const QPushButton *discard = box.addButton(QObject::tr("Discard"), QMessageBox::DestructiveRole);
    box.setDefaultButton(box.addButton(QObject::tr("Keep"), QMessageBox::RejectRole));
    box.exec();
    return box.clickedButton() == discard;
}

} // namespace DiscardConfirmation
