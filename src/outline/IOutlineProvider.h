// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "OutlineSymbol.h"

#include <QByteArray>

#include <vector>

// Computes the outline of a file (see OutlineProviders::create). Each
// Outline pane owns its provider, chosen according to the file's language.
class IOutlineProvider {
public:
    virtual ~IOutlineProvider() = default;

    // Outline of `text`, the file's complete UTF-8 text.
    virtual std::vector<OutlineSymbol> symbols(const QByteArray &text) = 0;
};
