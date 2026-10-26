// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// What the Option key does in a terminal on macOS. Compose: it types the
// character the keyboard layout gives it (⌥5 is "{" on a French keyboard),
// as in Terminal.app by default. Meta: it prefixes the key with Escape (⌥b
// moves a shell back a word). Alt is always Meta on the other systems.
enum class OptionKey {
    Compose,
    Meta,
};
