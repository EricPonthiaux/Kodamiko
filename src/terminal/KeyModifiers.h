// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "OptionKey.h"

#include <QFlags>
#include <QString>
#include <Qt>

#include <vterm.h>

// The terminal's modifiers for a key press, as the system names the keys.
// One source per system.
namespace KeyModifiers {

// The modifiers of a key press, for libvterm.
VTermModifier toVTerm(Qt::KeyboardModifiers modifiers);

// The modifiers sent with the text a key press produced: on macOS, Option
// already composed the text unless it acts as Meta.
VTermModifier forText(VTermModifier modifier, OptionKey optionKey);

// True when `text` is a character the keyboard layout composed with AltGr,
// which Windows reports as Ctrl+Alt (AltGr+E is "€" on a French keyboard):
// it is typed as is, without the modifiers. False on the systems where
// AltGr is a key of its own.
bool isComposedByAltGr(Qt::KeyboardModifiers modifiers, const QString &text);

// VTermModifier is a set of bit flags: combinations such as Shift+Ctrl are
// valid values even though the C enum does not name them.
inline VTermModifier fromBits(int bits)
{
    return static_cast<VTermModifier>(bits); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange): bit flags
}

} // namespace KeyModifiers
