// SPDX-License-Identifier: GPL-3.0-only

#include "NameSuggestions.h"

#include "CodeScan.h"

#include <QHash>
#include <QObject>
#include <QRegularExpression>
#include <QSet>

#include <optional>

namespace {

bool isJava(const QString &languageId)
{
    return languageId == QLatin1String("java");
}

// Words before a type that still declare a name after it.
const QSet<QByteArray> &qualifierWords()
{
    static const QSet<QByteArray> words = {
        "const",  "static",  "final",     "volatile",  "constexpr",    "constinit", "inline",  "unsigned",
        "signed", "long",    "short",     "struct",    "enum",         "union",     "mutable", "register",
        "public", "private", "protected", "transient", "thread_local", "extern",    "typename"};
    return words;
}

// Words that are no type: "return x", "new Foo".
const QSet<QString> &notTypes()
{
    static const QSet<QString> words = {
        "return",     "new",        "delete",   "throw",  "case",    "goto",      "else",     "do",       "using",
        "namespace",  "class",      "struct",   "enum",   "union",   "template",  "typename", "operator", "sizeof",
        "co_return",  "co_await",   "co_yield", "public", "private", "protected", "import",   "package",  "extends",
        "implements", "instanceof", "yield",    "auto",   "var",     "void",      "const",    "static",   "final",
        "and",        "or",         "not",      "if",     "while",   "for",       "switch",   "catch",    "try",
        "default",    "break",      "continue", "this",   "true",    "false",     "nullptr",  "null",     "friend",
        "virtual",    "typedef",    "decltype", "throws", "assert",  "super",     "volatile", "inline",   "explicit"};
    return words;
}

const QSet<QString> &primitives()
{
    static const QSet<QString> words = {"int",    "char",      "bool",    "float",    "double",  "long",
                                        "short",  "unsigned",  "signed",  "size_t",   "wchar_t", "boolean",
                                        "byte",   "qsizetype", "qreal",   "uint",     "qint64",  "quint64",
                                        "qint32", "quint32",   "ssize_t", "ptrdiff_t"};
    return words;
}

const QSet<QString> &reservedNames()
{
    static const QSet<QString> words = {"class",  "new",      "default", "delete", "operator", "template",  "namespace",
                                        "int",    "char",     "bool",    "float",  "double",   "long",      "short",
                                        "union",  "register", "this",    "switch", "case",     "do",        "for",
                                        "if",     "else",     "while",   "return", "enum",     "struct",    "goto",
                                        "try",    "catch",    "throw",   "public", "private",  "protected", "virtual",
                                        "static", "const",    "auto",    "void",   "package",  "import",    "interface",
                                        "final",  "boolean",  "byte",    "native", "super",    "volatile",  "transient",
                                        "and",    "or",       "not",     "export", "module",   "object"};
    return words;
}

bool looksLikeType(const QString &type, const QString &languageId)
{
    QString last = type;
    const int open = int(last.indexOf(QLatin1Char('<')));
    if (open >= 0)
        last = last.left(open);
    last.remove(QStringLiteral("[]"));
    for (const QLatin1String separator : {QLatin1String("::"), QLatin1String(".")}) {
        const int at = int(last.lastIndexOf(separator));
        if (at >= 0)
            last = last.mid(at + separator.size());
    }
    if (last.isEmpty() || notTypes().contains(last))
        return false;
    if (primitives().contains(last) || last[0].isUpper())
        return true;
    if (isJava(languageId))
        return false;
    // C and C++: "std::string", "size_t", "vector<int>".
    return type.contains(QLatin1String("::")) || open >= 0 || last.endsWith(QLatin1String("_t"));
}

QString lowerFirst(const QString &word)
{
    if (word.isEmpty())
        return word;
    // "HTML" → "html", "URLLoader" handled word by word.
    if (word.size() > 1 && word == word.toUpper())
        return word.toLower();
    return word.left(1).toLower() + word.mid(1);
}

// "QListWidget" → "List", "Widget" (the "Q" of Qt, the "I" of an
// interface left out); "HTMLParser" → "HTML", "Parser".
QStringList camelWords(QString name)
{
    static const QRegularExpression prefix(QStringLiteral("^[A-Z](?=[A-Z][a-z])"));
    name.remove(prefix);
    QStringList words;
    int start = 0;
    for (int i = 1; i < name.size(); ++i) {
        const QChar previous = name[i - 1];
        const QChar current = name[i];
        const bool next = i + 1 < name.size() && name[i + 1].isLower();
        if ((current.isUpper() && previous.isLower()) || (current.isUpper() && previous.isUpper() && next) ||
            (current.isDigit() != previous.isDigit() && current.isLetter())) {
            words.append(name.mid(start, i - start));
            start = i;
        }
    }
    words.append(name.mid(start));
    words.removeAll(QString());
    return words;
}

QString plural(const QString &name)
{
    if (name.isEmpty())
        return name;
    static const QRegularExpression consonantY(QStringLiteral("[^aeiou]y$"));
    if (consonantY.match(name).hasMatch())
        return name.left(name.size() - 1) + QStringLiteral("ies");
    for (const QLatin1String ending :
         {QLatin1String("s"), QLatin1String("x"), QLatin1String("z"), QLatin1String("ch"), QLatin1String("sh")}) {
        if (name.endsWith(ending))
            return name + QStringLiteral("es");
    }
    return name + QLatin1Char('s');
}

// The type's name and its template arguments: "std::vector<Item>" →
// "vector", {"Item"}.
QString splitTemplate(const QString &type, QStringList *arguments)
{
    const int open = int(type.indexOf(QLatin1Char('<')));
    QString base = open < 0 ? type : type.left(open);
    if (open >= 0 && type.endsWith(QLatin1Char('>')))
        *arguments = CodeScan::splitTopLevel(type.mid(open + 1, type.size() - open - 2));
    for (const QLatin1String separator : {QLatin1String("::"), QLatin1String(".")}) {
        const int at = int(base.lastIndexOf(separator));
        if (at >= 0)
            base = base.mid(at + separator.size());
    }
    return base.trimmed();
}

bool isDeclaratorSeparator(char c)
{
    return c == ' ' || c == '\t' || c == '*' || c == '&';
}

bool isStatementBoundary(char c)
{
    return c == ';' || c == '{' || c == '}' || c == '\n';
}

// The "<" opening the template arguments that the ">" at text[close]
// closes; nullopt if a statement boundary comes first, or if nothing
// precedes the "<".
std::optional<int> templateOpening(const QByteArray &text, int close)
{
    int depth = 0;
    for (int k = close; k >= 0; --k) {
        const char c = text[k];
        if (c == '>')
            ++depth;
        else if (c == '<' && --depth == 0)
            return k > 0 ? std::optional<int>(k) : std::nullopt;
        else if (isStatementBoundary(c))
            return std::nullopt;
    }
    return std::nullopt;
}

// The start of the type that ends at `end`, read backwards: names, "::",
// ".", "<...>", "[]". nullopt if its template arguments are not closed.
std::optional<int> typeStartBefore(const QByteArray &text, int end, const QString &languageId)
{
    int i = end;
    while (i > 0) {
        const char c = text[i - 1];
        const bool isNamePart = CodeScan::isIdentifierByte(c) || (c == '.' && isJava(languageId));
        const bool isArrayBrackets = c == ']' && i >= 2 && text[i - 2] == '[';
        const bool isScope = c == ':' && i >= 2 && text[i - 2] == ':';
        if (isNamePart) {
            --i;
        } else if (c == '>') {
            const std::optional<int> opening = templateOpening(text, i - 1);
            if (!opening)
                return std::nullopt;
            i = *opening;
        } else if (isArrayBrackets || isScope) {
            i -= 2;
        } else {
            break;
        }
    }
    return i;
}

// The start of the qualifiers ("const", "static"…) before text[typeStart].
int qualifiersStart(const QByteArray &text, int typeStart)
{
    int before = typeStart;
    while (true) {
        while (before > 0 && (text[before - 1] == ' ' || text[before - 1] == '\t'))
            --before;
        int wordStart = before;
        while (wordStart > 0 && CodeScan::isIdentifierByte(text[wordStart - 1]))
            --wordStart;
        if (wordStart == before || !qualifierWords().contains(text.mid(wordStart, before - wordStart)))
            return before;
        before = wordStart;
    }
}

// Whether a declaration can start at text[before]: at the start of a
// statement or a parameter, or after a label's ":" (not a "::").
bool isDeclarationStart(const QByteArray &text, int before)
{
    if (before == 0)
        return true;
    const char previous = text[before - 1];
    if (QByteArrayView("\n;{}(,").contains(previous))
        return true;
    const bool isScope = before >= 2 && text[before - 2] == ':';
    return previous == ':' && !isScope;
}

QString cleanedType(const QString &type)
{
    QString cleaned = type;
    cleaned.remove(QRegularExpression(QStringLiteral("\\b(?:const|volatile|struct|enum|union|typename)\\b")));
    cleaned.remove(QLatin1Char('*'));
    cleaned.remove(QLatin1Char('&'));
    cleaned = cleaned.simplified();
    cleaned.remove(QLatin1Char(' '));
    return cleaned;
}

// Drops the empty, repeated and reserved names, keeping the order.
QStringList usableNames(const QStringList &candidates)
{
    QStringList names;
    for (const QString &name : candidates) {
        if (!name.isEmpty() && !names.contains(name) && !reservedNames().contains(name))
            names.append(name);
    }
    return names;
}

QStringList plurals(const QStringList &names)
{
    QStringList result;
    result.reserve(names.size());
    for (const QString &name : names)
        result.append(plural(name));
    return result;
}

const QSet<QString> &containerTypes()
{
    static const QSet<QString> types = {"vector",          "list",        "deque",   "set",     "unordered_set",
                                        "multiset",        "QList",       "QVector", "QSet",    "QStack",
                                        "QQueue",          "span",        "array",   "List",    "ArrayList",
                                        "LinkedList",      "Set",         "HashSet", "TreeSet", "Collection",
                                        "Iterable",        "Queue",       "Deque",   "Stream",  "initializer_list",
                                        "QVarLengthArray", "forward_list"};
    return types;
}

// Types named after what they hold: a pointer to a Widget is a "widget".
const QSet<QString> &wrapperTypes()
{
    static const QSet<QString> types = {
        "unique_ptr", "shared_ptr", "weak_ptr",          "optional", "QPointer", "QSharedPointer",   "QScopedPointer",
        "Optional",   "atomic",     "reference_wrapper", "QFuture",  "Future",   "CompletableFuture"};
    return types;
}

const QSet<QString> &stringTypes()
{
    static const QSet<QString> types = {"string", "String", "QString", "wstring", "string_view", "QStringView"};
    return types;
}

QStringList primitiveNames(const QString &type)
{
    static const QHash<QString, QStringList> byType = {{"int", {"i", "n", "count"}},
                                                       {"long", {"l", "n"}},
                                                       {"short", {"s"}},
                                                       {"size_t", {"size", "n", "i"}},
                                                       {"qsizetype", {"size", "n", "i"}},
                                                       {"char", {"c"}},
                                                       {"wchar_t", {"c"}},
                                                       {"bool", {"b", "enabled"}},
                                                       {"boolean", {"b", "enabled"}},
                                                       {"float", {"f", "value"}},
                                                       {"double", {"d", "value"}},
                                                       {"qreal", {"value"}},
                                                       {"byte", {"b"}}};
    return byType.value(type, {QStringLiteral("value")});
}

// snake_case: "my_type_t" → "my_type", "type".
QStringList snakeCaseNames(const QString &type)
{
    QStringList words = type.split(QLatin1Char('_'), Qt::SkipEmptyParts);
    if (words.size() > 1 && words.last() == QLatin1String("t"))
        words.removeLast();
    QStringList names;
    for (qsizetype k = 0; k < words.size(); ++k)
        names.append(words.mid(k).join(QLatin1Char('_')).toLower());
    return names;
}

// "TextEditor" → "textEditor", "editor".
QStringList camelCaseNames(const QString &type)
{
    const QStringList words = camelWords(type);
    QStringList names;
    for (qsizetype k = 0; k < words.size(); ++k) {
        QString name = lowerFirst(words[k]);
        for (qsizetype j = k + 1; j < words.size(); ++j)
            name += words[j];
        names.append(name);
    }
    return names;
}

} // namespace

bool NameSuggestionProvider::handles(const CompletionContext &context) const
{
    const QString &language = context.languageId;
    return context.triggerCharacter.isNull() &&
           (language == QLatin1String("c") || language == QLatin1String("cpp") || isJava(language));
}

QString NameSuggestionProvider::declaredTypeAt(const QByteArray &text, int position, const QString &languageId)
{
    int end = position;
    while (end > 0 && isDeclaratorSeparator(text[end - 1]))
        --end;
    const bool isSeparated = end != position;
    if (!isSeparated || end == 0)
        return {};
    const std::optional<int> typeStart = typeStartBefore(text, end, languageId);
    if (!typeStart || *typeStart == end)
        return {};
    const bool startsWithDigit = text[*typeStart] >= '0' && text[*typeStart] <= '9';
    if (startsWithDigit)
        return {};
    if (!isDeclarationStart(text, qualifiersStart(text, *typeStart)))
        return {};
    const QString type = QString::fromUtf8(text.mid(*typeStart, end - *typeStart)).trimmed();
    return looksLikeType(type, languageId) ? type : QString();
}

QStringList NameSuggestionProvider::namesFor(const QString &type, const QString &languageId)
{
    const QString cleaned = cleanedType(type);
    if (cleaned.endsWith(QLatin1String("[]")))
        return usableNames(plurals(namesFor(cleaned.left(cleaned.size() - 2), languageId)));
    QStringList arguments;
    const QString base = splitTemplate(cleaned, &arguments);
    const bool hasArguments = !arguments.isEmpty();
    if (hasArguments && containerTypes().contains(base))
        return usableNames(plurals(namesFor(arguments.first(), languageId)) << lowerFirst(base));
    if (hasArguments && wrapperTypes().contains(base))
        return namesFor(arguments.first(), languageId);
    if (primitives().contains(base))
        return usableNames(primitiveNames(base));
    if (stringTypes().contains(base))
        return usableNames({QStringLiteral("text"), QStringLiteral("name"), QStringLiteral("s")});
    if (base.contains(QLatin1Char('_')) || base == base.toLower())
        return usableNames(snakeCaseNames(base));
    QStringList names = usableNames(camelCaseNames(base));
    if (!names.isEmpty() || base.isEmpty())
        return names;
    return usableNames({isJava(languageId) ? QStringLiteral("a") + base : lowerFirst(base) + QLatin1Char('_')});
}

void NameSuggestionProvider::complete(const CompletionContext &context, const Callback &done)
{
    QList<CompletionItem> items;
    const QString type = declaredTypeAt(context.text, context.prefixStart, context.languageId);
    if (!type.isEmpty()) {
        const QStringList names = namesFor(type, context.languageId);
        for (int i = 0; i < names.size(); ++i) {
            CompletionItem item;
            item.label = names[i];
            item.kind = CompletionItem::Kind::Variable;
            item.detail = QObject::tr("name for %1").arg(type);
            // A name is what goes here: above whatever else matches.
            item.priority = 30;
            item.order = i;
            items.append(item);
        }
    }
    done(items, false);
}
