// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

class QWidget;

// Asking before changes are discarded, and saying they can be restored.
namespace DiscardConfirmation {

// Where the copy taken before a discard can be restored from.
QString hint();

// Asks `question` in a popup over `parent`'s window, with `details` and
// hint(), and waits for the answer: true when the user chose to discard.
[[nodiscard]] bool ask(QWidget *parent, const QString &question, const QString &details);

} // namespace DiscardConfirmation
