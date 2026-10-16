#pragma once

#include <QString>

#include <vector>

enum class SymbolKind { Namespace, Class, Struct, Interface, Enum, Type, Function, Method, Field, Macro, Heading, Target };

// Element of a file's outline: a function, a class, a heading...
// Lines start at 0; `position` is a byte offset in the UTF-8 text,
// like Scintilla positions.
struct OutlineSymbol {
    QString name;
    SymbolKind kind = SymbolKind::Function;
    int position = 0; // start of the name, where to place the cursor
    int startLine = 0;
    int endLine = 0;  // inclusive
    bool declarationOnly = false; // prototype, signature: the definition is elsewhere
    std::vector<OutlineSymbol> children;
};

// Flat symbol, before nesting. The range [start, end) (in bytes or in
// lines, as long as all symbols use the same unit) contains those of its
// children.
struct RangedSymbol {
    OutlineSymbol symbol;
    int start = 0;
    int end = 0;
    bool declarationOnly = false; // prototype, signature
};

namespace OutlineSymbols {

// Nests symbols according to their ranges: each becomes the child of the
// smallest symbol that strictly contains it. A function contained in a
// class, struct or interface becomes a method; a mere declaration
// contained in a function is ignored (in C++, "Foo f(x);" in a function
// body also reads as a prototype).
std::vector<OutlineSymbol> nest(std::vector<RangedSymbol> flat);

} // namespace OutlineSymbols
