#pragma once

#include <QString>

#include <vector>

enum class SymbolKind { Namespace, Class, Struct, Interface, Enum, Type, Function, Method, Field, Macro, Heading, Target };

// Élément de la structure d'un fichier : une fonction, une classe, un titre...
// Les lignes commencent à 0 ; `position` est un décalage en octets dans le
// texte UTF-8, comme les positions Scintilla.
struct OutlineSymbol {
    QString name;
    SymbolKind kind = SymbolKind::Function;
    int position = 0; // début du nom, où placer le curseur
    int startLine = 0;
    int endLine = 0;  // incluse
    std::vector<OutlineSymbol> children;
};

// Symbole à plat, avant imbrication. La plage [start, end[ (en octets ou en
// lignes, du moment que tous les symboles utilisent la même unité) contient
// celles de ses enfants.
struct RangedSymbol {
    OutlineSymbol symbol;
    int start = 0;
    int end = 0;
    bool declarationOnly = false; // prototype, signature
};

namespace OutlineSymbols {

// Imbrique les symboles d'après leurs plages : chacun devient l'enfant du
// plus petit symbole qui le contient strictement. Une fonction contenue dans
// une classe, une structure ou une interface devient une méthode ; une simple
// déclaration contenue dans une fonction est ignorée (en C++, « Foo f(x); »
// dans un corps de fonction se lit aussi comme un prototype).
std::vector<OutlineSymbol> nest(std::vector<RangedSymbol> flat);

} // namespace OutlineSymbols
