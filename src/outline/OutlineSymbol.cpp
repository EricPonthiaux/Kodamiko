// SPDX-License-Identifier: GPL-3.0-only

#include "OutlineSymbol.h"

#include <algorithm>

namespace OutlineSymbols {
namespace {

bool isTypeContainer(SymbolKind kind)
{
    return kind == SymbolKind::Class || kind == SymbolKind::Struct || kind == SymbolKind::Interface;
}

bool isFunction(SymbolKind kind)
{
    return kind == SymbolKind::Function || kind == SymbolKind::Method;
}

bool encloses(const RangedSymbol &outer, const RangedSymbol &inner)
{
    const bool sameRange = outer.start == inner.start && outer.end == inner.end;
    return outer.start <= inner.start && inner.end <= outer.end && !sameRange;
}

// Pops the containers that end before `current` (or share its exact range).
void dropNonEnclosingAncestors(const std::vector<RangedSymbol> &flat,
                               const RangedSymbol &current,
                               std::vector<size_t> &ancestors)
{
    while (!ancestors.empty() && !encloses(flat[ancestors.back()], current))
        ancestors.pop_back();
}

std::vector<OutlineSymbol> build(std::vector<RangedSymbol> &flat,
                                 const std::vector<std::vector<size_t>> &children,
                                 const std::vector<size_t> &indices)
{
    std::vector<OutlineSymbol> result;
    result.reserve(indices.size());
    for (const size_t index : indices) {
        OutlineSymbol symbol = std::move(flat[index].symbol);
        symbol.children = build(flat, children, children[index]);
        result.push_back(std::move(symbol));
    }
    return result;
}

} // namespace

std::vector<OutlineSymbol> nest(std::vector<RangedSymbol> flat)
{
    // Text order; for equal starts, the longest (the container) first.
    std::stable_sort(flat.begin(), flat.end(), [](const RangedSymbol &a, const RangedSymbol &b) {
        if (a.start != b.start)
            return a.start < b.start;
        return a.end > b.end;
    });

    // The same construct can be captured twice (two patterns, or
    // "int a, b;" which yields two fields with the same range, which are distinct).
    flat.erase(std::unique(flat.begin(), flat.end(),
                           [](const RangedSymbol &a, const RangedSymbol &b) {
                               return a.start == b.start && a.end == b.end && a.symbol.name == b.symbol.name;
                           }),
               flat.end());

    std::vector<std::vector<size_t>> children(flat.size());
    std::vector<size_t> roots;
    std::vector<size_t> ancestors; // chain of containers of the current symbol
    for (size_t i = 0; i < flat.size(); ++i) {
        RangedSymbol &current = flat[i];
        dropNonEnclosingAncestors(flat, current, ancestors);
        if (ancestors.empty()) {
            roots.push_back(i);
        } else {
            const SymbolKind parentKind = flat[ancestors.back()].symbol.kind;
            if (current.declarationOnly && isFunction(parentKind))
                continue;
            if (current.symbol.kind == SymbolKind::Function && isTypeContainer(parentKind))
                current.symbol.kind = SymbolKind::Method;
            children[ancestors.back()].push_back(i);
        }
        ancestors.push_back(i);
    }
    return build(flat, children, roots);
}

} // namespace OutlineSymbols
