// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "OptionKey.h"

#include <QFlags>
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

// VTermModifier is a set of bit flags: combinations such as Shift+Ctrl are
// valid values even though the C enum does not name them.
inline VTermModifier fromBits(int bits)
{
    return static_cast<VTermModifier>(bits); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange): bit flags
}

} // namespace KeyModifiers
