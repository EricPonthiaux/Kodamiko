// SPDX-License-Identifier: GPL-3.0-only

#include "DiscardConfirmation.h"

#include "workspace/ChoicePopup.h"

namespace DiscardConfirmation {

QString hint()
{
    return QObject::tr("A copy is kept: Git menu ▸ Restore a Snapshot.");
}

// "Discard" rather than "Cancel", which would be confused with the Cancel button.
bool ask(QWidget *parent, const QString &question, const QString &details)
{
    bool discarded = false;
    auto *popup = new ChoicePopup({.title = QObject::tr("Discard Changes"),
                                   .location = {},
                                   .message = question,
                                   .details = details.isEmpty() ? hint() : details + QLatin1Char(' ') + hint()},
                                  {{QObject::tr("Discard"), [&discarded] { discarded = true; }}}, parent);
    popup->setCancelText(QObject::tr("Keep"));
    popup->focusCancel();
    popup->exec();
    return discarded;
}

} // namespace DiscardConfirmation
