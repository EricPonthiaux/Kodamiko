// SPDX-License-Identifier: GPL-3.0-only

#include "terminal/KeyModifiers.h"

// Qt calls the Cmd key "Control" and the real Ctrl key "Meta": the latter
// is the one that produces the terminal's Ctrl+C. Cmd belongs to the
// application.
VTermModifier KeyModifiers::toVTerm(Qt::KeyboardModifiers modifiers)
{
    int result = VTERM_MOD_NONE;
    if (modifiers & Qt::ShiftModifier)
        result |= VTERM_MOD_SHIFT;
    if (modifiers & Qt::AltModifier)
        result |= VTERM_MOD_ALT;
    if (modifiers & Qt::MetaModifier)
        result |= VTERM_MOD_CTRL;
    return fromBits(result);
}

VTermModifier KeyModifiers::forText(VTermModifier modifier, OptionKey optionKey)
{
    switch (optionKey) {
    case OptionKey::Compose:
        return fromBits(modifier & ~VTERM_MOD_ALT);
    case OptionKey::Meta:
        return modifier;
    }
    return modifier;
}
