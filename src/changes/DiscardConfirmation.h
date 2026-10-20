// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

class QWidget;

// Asking before changes are discarded, and saying they can be restored.
namespace DiscardConfirmation {

// Where the copy taken before a discard can be restored from.
QString hint();

// Asks `question` in a warning box above `parent`, with `details` and hint();
// true when the user chose to discard.
[[nodiscard]] bool ask(QWidget *parent, const QString &question, const QString &details);

} // namespace DiscardConfirmation
