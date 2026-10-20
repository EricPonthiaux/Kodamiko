// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "IOutlineProvider.h"

#include <QString>

#include <memory>

namespace OutlineProviders {

// Outline provider for the language (LanguageRegistry identifier):
// tree-sitter outline.scm query for C, C++, Java, JavaScript and
// TypeScript, line-by-line parsing for Markdown and CMake, nullptr otherwise.
std::unique_ptr<IOutlineProvider> create(const QString &languageId);

} // namespace OutlineProviders
