// SPDX-License-Identifier: GPL-3.0-only

#include "SymbolIcons.h"

#include "ui/LetterBadges.h"

#include <QColor>
#include <QCoreApplication>

namespace {

struct KindStyle {
    char letter{};
    QColor color;
    const char *name{};
};

KindStyle styleOf(SymbolKind kind)
{
    switch (kind) {
    case SymbolKind::Namespace:
        return {'N', QColor("#8a94a6"), QT_TRANSLATE_NOOP("SymbolIcons", "Namespace")};
    case SymbolKind::Class:
        return {'C', QColor("#d19a40"), QT_TRANSLATE_NOOP("SymbolIcons", "Class")};
    case SymbolKind::Struct:
        return {'S', QColor("#c7783e"), QT_TRANSLATE_NOOP("SymbolIcons", "Struct")};
    case SymbolKind::Interface:
        return {'I', QColor("#3fa7a0"), QT_TRANSLATE_NOOP("SymbolIcons", "Interface")};
    case SymbolKind::Enum:
        return {'E', QColor("#b5a13a"), QT_TRANSLATE_NOOP("SymbolIcons", "Enum")};
    case SymbolKind::Type:
        return {'T', QColor("#4fa36b"), QT_TRANSLATE_NOOP("SymbolIcons", "Type")};
    case SymbolKind::Function:
        return {'f', QColor("#9b6fd1"), QT_TRANSLATE_NOOP("SymbolIcons", "Function")};
    case SymbolKind::Method:
        return {'m', QColor("#a573c9"), QT_TRANSLATE_NOOP("SymbolIcons", "Method")};
    case SymbolKind::Field:
        return {'v', QColor("#4b8fd6"), QT_TRANSLATE_NOOP("SymbolIcons", "Field")};
    case SymbolKind::Macro:
        return {'#', QColor("#c25b6b"), QT_TRANSLATE_NOOP("SymbolIcons", "Macro")};
    case SymbolKind::Heading:
        return {'H', QColor("#4b8fd6"), QT_TRANSLATE_NOOP("SymbolIcons", "Heading")};
    case SymbolKind::Target:
        return {'T', QColor("#4fa36b"), QT_TRANSLATE_NOOP("SymbolIcons", "Target")};
    }
    return {'?', QColor("#8a94a6"), ""};
}

} // namespace

namespace SymbolIcons {

QIcon icon(SymbolKind kind)
{
    const KindStyle style = styleOf(kind);
    return LetterBadges::icon(QLatin1Char(style.letter), style.color);
}

QString name(SymbolKind kind)
{
    return QCoreApplication::translate("SymbolIcons", styleOf(kind).name);
}

} // namespace SymbolIcons
