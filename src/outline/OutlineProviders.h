#pragma once

#include "IOutlineProvider.h"

#include <QString>

#include <memory>

namespace OutlineProviders {

// Fournisseur de structure pour le langage (identifiant de LanguageRegistry) :
// requête tree-sitter outline.scm pour C, C++, Java, JavaScript et
// TypeScript, analyse ligne à ligne pour Markdown et CMake, nullptr sinon.
std::unique_ptr<IOutlineProvider> create(const QString &languageId);

} // namespace OutlineProviders
