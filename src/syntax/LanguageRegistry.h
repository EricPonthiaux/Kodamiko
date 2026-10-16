#pragma once

#include <QString>

// Determines a file's language from its name. The returned identifiers
// ("cpp", "java", "json"...) are the ones HighlighterFactory expects;
// "text" denotes a file without highlighting.
namespace LanguageRegistry {

inline const QString plainText = QStringLiteral("text");

QString languageForFile(const QString &filePath);

} // namespace LanguageRegistry
