#include "LocalCompletion.h"

#include "navigation/SymbolIndex.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cstdlib>

namespace {

bool isIdentifierByte(char c)
{
    // Bytes of non-ASCII characters (é) count: identifiers can hold them.
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$'
        || static_cast<unsigned char>(c) >= 0x80;
}

CompletionItem::Kind kindOf(SymbolKind kind)
{
    switch (kind) {
    case SymbolKind::Namespace: return CompletionItem::Kind::Namespace;
    case SymbolKind::Class: return CompletionItem::Kind::Class;
    case SymbolKind::Struct: return CompletionItem::Kind::Struct;
    case SymbolKind::Interface: return CompletionItem::Kind::Interface;
    case SymbolKind::Enum: return CompletionItem::Kind::Enum;
    case SymbolKind::Type: return CompletionItem::Kind::Type;
    case SymbolKind::Function: return CompletionItem::Kind::Function;
    case SymbolKind::Method: return CompletionItem::Kind::Method;
    case SymbolKind::Field: return CompletionItem::Kind::Field;
    case SymbolKind::Macro: return CompletionItem::Kind::Macro;
    case SymbolKind::Heading:
    case SymbolKind::Target: break;
    }
    return CompletionItem::Kind::Text;
}

// Languages whose symbols can be used in one another.
QString family(const QString &languageId)
{
    if (languageId == QLatin1String("c") || languageId == QLatin1String("cpp"))
        return QStringLiteral("c");
    if (languageId == QLatin1String("javascript") || languageId == QLatin1String("typescript")
        || languageId == QLatin1String("tsx"))
        return QStringLiteral("js");
    return languageId;
}

} // namespace

bool isIdentifierCharacter(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$');
}

// DocumentWordsProvider

bool DocumentWordsProvider::handles(const CompletionContext &) const
{
    return true;
}

QStringList DocumentWordsProvider::words(const QByteArray &text, int exclude)
{
    QHash<QString, int> distance; // nearest occurrence
    const int size = int(text.size());
    for (int i = 0; i < size;) {
        if (!isIdentifierByte(text[i])) {
            ++i;
            continue;
        }
        const int start = i;
        while (i < size && isIdentifierByte(text[i]))
            ++i;
        if (i - start < 3 || (text[start] >= '0' && text[start] <= '9'))
            continue;
        if (exclude >= start && exclude <= i)
            continue; // the word being typed
        const QString word = QString::fromUtf8(text.constData() + start, i - start);
        const int d = std::abs(start - exclude);
        auto it = distance.find(word);
        if (it == distance.end())
            distance.insert(word, d);
        else
            it.value() = std::min(it.value(), d);
    }
    QStringList result = distance.keys();
    std::sort(result.begin(), result.end(),
              [&distance](const QString &a, const QString &b) { return distance.value(a) < distance.value(b); });
    return result;
}

void DocumentWordsProvider::complete(const CompletionContext &context, const Callback &done)
{
    QList<CompletionItem> items;
    // After ".", only a language server knows the members.
    if (!context.triggerCharacter.isNull()) {
        done(items, false);
        return;
    }
    const QStringList found = words(context.text, context.position);
    items.reserve(found.size());
    for (int i = 0; i < found.size(); ++i) {
        CompletionItem item;
        item.label = found[i];
        item.kind = CompletionItem::Kind::Text;
        // The 20 nearest words rank above keywords: in the middle of code,
        // a name of the file is the likelier word.
        item.priority = i < 20 ? 4 : 1;
        items.append(item);
    }
    done(items, false);
}

// KeywordProvider

QStringList KeywordProvider::keywords(const QString &languageId)
{
    static const QHash<QString, QStringList> table = {
        {QStringLiteral("c"),
         {"auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern",
          "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return", "short", "signed", "sizeof",
          "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "bool", "true",
          "false", "NULL", "#include", "#define", "#ifdef", "#ifndef", "#endif", "#pragma"}},
        {QStringLiteral("cpp"),
         {"alignas", "alignof", "auto", "bool", "break", "case", "catch", "char", "class", "concept", "const",
          "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await", "co_return", "co_yield",
          "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern",
          "false", "final", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new",
          "noexcept", "nullptr", "operator", "override", "private", "protected", "public", "reinterpret_cast",
          "requires", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
          "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename",
          "union", "unsigned", "using", "virtual", "void", "volatile", "while", "std", "#include", "#define",
          "#ifdef", "#ifndef", "#endif", "#pragma"}},
        {QStringLiteral("java"),
         {"abstract", "assert", "boolean", "break", "byte", "case", "catch", "char", "class", "continue", "default",
          "do", "double", "else", "enum", "extends", "final", "finally", "float", "for", "if", "implements",
          "import", "instanceof", "int", "interface", "long", "native", "new", "null", "package", "private",
          "protected", "public", "record", "return", "sealed", "short", "static", "super", "switch", "synchronized",
          "this", "throw", "throws", "transient", "true", "false", "try", "var", "void", "volatile", "while",
          "yield", "String", "Override"}},
        {QStringLiteral("js"),
         {"async", "await", "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete",
          "else", "export", "extends", "false", "finally", "for", "function", "if", "import", "in", "instanceof",
          "let", "new", "null", "return", "static", "super", "switch", "this", "throw", "true", "try", "typeof",
          "undefined", "var", "void", "while", "yield", "console", "document", "window", "Promise", "Array",
          "Object", "String", "Number", "Boolean", "JSON", "Math", "Map", "Set"}},
        {QStringLiteral("ts"),
         {"abstract", "any", "as", "boolean", "declare", "enum", "implements", "interface", "keyof", "namespace",
          "never", "number", "private", "protected", "public", "readonly", "string", "type", "unknown",
          "satisfies"}},
        {QStringLiteral("python"),
         {"and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del", "elif", "else",
          "except", "False", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "None",
          "nonlocal", "not", "or", "pass", "raise", "return", "True", "try", "while", "with", "yield", "self",
          "print", "len", "range", "dict", "list", "set", "tuple", "str", "int", "float", "isinstance"}},
        {QStringLiteral("bash"),
         {"if", "then", "else", "elif", "fi", "for", "while", "until", "do", "done", "case", "esac", "function",
          "in", "local", "export", "readonly", "return", "echo", "printf", "read", "shift", "source", "exit"}},
        {QStringLiteral("cmake"),
         {"add_executable", "add_library", "add_subdirectory", "add_test", "cmake_minimum_required", "else",
          "endforeach", "endfunction", "endif", "endmacro", "find_package", "foreach", "function", "if",
          "include", "install", "macro", "message", "option", "project", "set", "target_compile_definitions",
          "target_compile_options", "target_include_directories", "target_link_libraries", "target_sources",
          "PRIVATE", "PUBLIC", "INTERFACE", "REQUIRED", "STATIC", "SHARED"}},
    };
    if (languageId == QLatin1String("typescript") || languageId == QLatin1String("tsx"))
        return table.value(QStringLiteral("js")) + table.value(QStringLiteral("ts"));
    if (languageId == QLatin1String("javascript"))
        return table.value(QStringLiteral("js"));
    return table.value(languageId);
}

bool KeywordProvider::handles(const CompletionContext &context) const
{
    return !keywords(context.languageId).isEmpty();
}

void KeywordProvider::complete(const CompletionContext &context, const Callback &done)
{
    QList<CompletionItem> items;
    // After ".", a member is expected, not a keyword.
    if (context.triggerCharacter.isNull()) {
        for (const QString &keyword : keywords(context.languageId)) {
            CompletionItem item;
            item.label = keyword;
            item.kind = CompletionItem::Kind::Keyword;
            item.priority = 2;
            items.append(item);
        }
    }
    done(items, false);
}

// ProjectSymbolsProvider

ProjectSymbolsProvider::ProjectSymbolsProvider(const SymbolIndex *index)
    : index_(index)
{
}

bool ProjectSymbolsProvider::handles(const CompletionContext &) const
{
    return index_ != nullptr;
}

void ProjectSymbolsProvider::complete(const CompletionContext &context, const Callback &done)
{
    const QString languages = family(context.languageId);
    QList<CompletionItem> items;
    if (!context.triggerCharacter.isNull()) {
        done(items, false);
        return;
    }
    QSet<QString> seen; // a name once, even if declared in several places
    for (const SymbolLocation &symbol : index_->symbols()) {
        if (family(symbol.languageId) != languages || symbol.name.isEmpty())
            continue;
        const CompletionItem::Kind kind = kindOf(symbol.kind);
        if (kind == CompletionItem::Kind::Text)
            continue;
        const QString key = symbol.name + QLatin1Char('\n') + symbol.container;
        if (seen.contains(key))
            continue;
        seen.insert(key);
        CompletionItem item;
        item.label = symbol.name;
        item.kind = kind;
        item.detail = symbol.container.isEmpty() ? QFileInfo(symbol.filePath).fileName() : symbol.container;
        // The current file's symbols first.
        item.priority = symbol.filePath == context.filePath ? 6 : 5;
        items.append(item);
    }
    done(items, false);
}
