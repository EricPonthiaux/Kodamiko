#pragma once

#include <QString>

// Détermine le langage d'un fichier d'après son nom. Les identifiants
// renvoyés (« cpp », « java », « json »...) sont ceux qu'attend
// HighlighterFactory ; « text » désigne un fichier sans coloration.
namespace LanguageRegistry {

inline const QString plainText = QStringLiteral("text");

QString languageForFile(const QString &filePath);

} // namespace LanguageRegistry
