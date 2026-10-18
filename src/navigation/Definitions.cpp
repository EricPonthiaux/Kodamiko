#include "Definitions.h"

#include <algorithm>

namespace Definitions {
namespace {

bool isType(SymbolKind kind)
{
    return kind == SymbolKind::Class || kind == SymbolKind::Struct || kind == SymbolKind::Interface ||
           kind == SymbolKind::Enum || kind == SymbolKind::Type;
}

bool isFunction(SymbolKind kind)
{
    return kind == SymbolKind::Function || kind == SymbolKind::Method;
}

// Smaller = more likely.
int tier(const SymbolLocation &symbol)
{
    if (isType(symbol.kind))
        return symbol.declarationOnly ? 2 : 0;
    if (isFunction(symbol.kind) || symbol.kind == SymbolKind::Macro)
        return symbol.declarationOnly ? 2 : 1;
    if (symbol.kind == SymbolKind::Field)
        return 3;
    return 4;
}

// C and C++ share their headers; JavaScript and TypeScript mix.
QString family(const QString &languageId)
{
    if (languageId == QLatin1String("c"))
        return QStringLiteral("cpp");
    if (languageId == QLatin1String("typescript") || languageId == QLatin1String("tsx"))
        return QStringLiteral("javascript");
    return languageId;
}

template <typename Predicate>
void keepIfAny(QList<SymbolLocation> &symbols, Predicate predicate)
{
    if (std::any_of(symbols.cbegin(), symbols.cend(), predicate))
        symbols.removeIf([&](const SymbolLocation &symbol) { return !predicate(symbol); });
}

// Class of the function containing the line, empty outside a method.
QString enclosingContainer(const QList<SymbolLocation> &fileSymbols, int line)
{
    const SymbolLocation *innermost = nullptr;
    for (const SymbolLocation &symbol : fileSymbols) {
        if (isFunction(symbol.kind) && !symbol.declarationOnly && symbol.startLine <= line && line <= symbol.endLine &&
            (!innermost || symbol.startLine >= innermost->startLine))
            innermost = &symbol;
    }
    return innermost ? innermost->container : QString();
}

} // namespace

Result resolve(const Request &request, QList<SymbolLocation> matches, const QList<SymbolLocation> &fileSymbols)
{
    const QString requestFamily = family(request.languageId);
    keepIfAny(matches, [&](const SymbolLocation &symbol) { return family(symbol.languageId) == requestFamily; });

    // Already on a symbol: toggle between its definition and its declarations.
    const auto current = std::find_if(matches.cbegin(), matches.cend(), [&](const SymbolLocation &symbol) {
        return symbol.filePath == request.filePath && symbol.position == request.position;
    });
    if (current != matches.cend()) {
        const SymbolLocation here = *current;
        matches.removeIf([&](const SymbolLocation &symbol) {
            return (symbol.filePath == here.filePath && symbol.position == here.position) ||
                   symbol.container != here.container || isType(symbol.kind) != isType(here.kind) ||
                   isFunction(symbol.kind) != isFunction(here.kind);
        });
        keepIfAny(matches,
                  [&](const SymbolLocation &symbol) { return symbol.declarationOnly != here.declarationOnly; });
        return {matches, matches.size() > 1};
    }

    if (!request.qualifier.isEmpty())
        keepIfAny(matches, [&](const SymbolLocation &symbol) { return symbol.container == request.qualifier; });
    const QString preferredContainer = request.qualifier.isEmpty() && !request.memberAccess
                                           ? enclosingContainer(fileSymbols, request.line)
                                           : QString();

    const auto rank = [&](const SymbolLocation &symbol) {
        const bool otherContainer = preferredContainer.isEmpty() || symbol.container != preferredContainer;
        return std::pair(tier(symbol), otherContainer);
    };
    std::stable_sort(matches.begin(), matches.end(), [&](const SymbolLocation &a, const SymbolLocation &b) {
        if (rank(a) != rank(b))
            return rank(a) < rank(b);
        const bool aHere = a.filePath == request.filePath;
        const bool bHere = b.filePath == request.filePath;
        if (aHere != bHere)
            return aHere;
        return std::pair(a.filePath, a.position) < std::pair(b.filePath, b.position);
    });
    const bool ambiguous = matches.size() > 1 && rank(matches[0]) == rank(matches[1]);
    return {matches, ambiguous};
}

} // namespace Definitions
