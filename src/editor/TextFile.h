#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

enum class LineEnding { Lf, CrLf, Cr };

// Ce qu'il faut savoir d'un fichier pour le réécrire à l'identique.
struct TextFileFormat {
    bool hasUtf8Bom = false;
    bool isLatin1 = false;   // fichier qui n'était pas de l'UTF-8 valide
    LineEnding lineEnding = LineEnding::Lf; // utilisée pour les nouvelles lignes
};

struct DecodedText {
    QString text;
    TextFileFormat format;
};

// Lecture et écriture des fichiers texte. Le texte n'est jamais converti :
// les fins de ligne restent celles du fichier, seule la fin de ligne des
// lignes nouvellement tapées dépend de `lineEnding`.
namespace TextFile {

DecodedText decode(const QByteArray &bytes);
QByteArray encode(const QString &text, const TextFileFormat &format);

// nullopt (avec `error`) si le fichier est illisible ou binaire.
std::optional<DecodedText> load(const QString &path, QString *error);
bool save(const QString &path, const QString &text, const TextFileFormat &format, QString *error);

} // namespace TextFile
