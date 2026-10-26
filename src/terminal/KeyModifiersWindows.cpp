// SPDX-License-Identifier: GPL-3.0-only

#include "KeyModifiers.h"

VTermModifier KeyModifiers::toVTerm(Qt::KeyboardModifiers modifiers)
{
    int result = VTERM_MOD_NONE;
    if (modifiers & Qt::ShiftModifier)
        result |= VTERM_MOD_SHIFT;
    if (modifiers & Qt::AltModifier)
        result |= VTERM_MOD_ALT;
    if (modifiers & Qt::ControlModifier)
        result |= VTERM_MOD_CTRL;
    return fromBits(result);
}

// Alt is Meta: the text goes with an Escape prefix, as in xterm.
VTermModifier KeyModifiers::forText(VTermModifier modifier, OptionKey /*optionKey*/)
{
    return modifier;
}
