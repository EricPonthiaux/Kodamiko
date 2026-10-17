#include "NameSuggestions.h"

#include "CodeScan.h"

#include <QHash>
#include <QObject>
#include <QRegularExpression>
#include <QSet>

namespace {

bool isJava(const QString &languageId)
{
    return languageId == QLatin1String("java");
}

// Words before a type that still declare a name after it.
const QSet<QByteArray> &qualifierWords()
{
    static const QSet<QByteArray> words = {"const", "static", "final", "volatile", "constexpr", "constinit",
                                           "inline", "unsigned", "signed", "long", "short", "struct", "enum",
                                           "union", "mutable", "register", "public", "private", "protected",
                                           "transient", "thread_local", "extern", "typename"};
    return words;
}

// Words that are no type: "return x", "new Foo".
const QSet<QString> &notTypes()
{
    static const QSet<QString> words = {
        "return", "new", "delete", "throw", "case", "goto", "else", "do", "using", "namespace", "class", "struct",
        "enum", "union", "template", "typename", "operator", "sizeof", "co_return", "co_await", "co_yield",
        "public", "private", "protected", "import", "package", "extends", "implements", "instanceof", "yield",
        "auto", "var", "void", "const", "static", "final", "and", "or", "not", "if", "while", "for", "switch",
        "catch", "try", "default", "break", "continue", "this", "true", "false", "nullptr", "null", "friend",
        "virtual", "typedef", "decltype", "throws", "assert", "super", "volatile", "inline", "explicit"};
    return words;
}

const QSet<QString> &primitives()
{
    static const QSet<QString> words = {"int", "char", "bool", "float", "double", "long", "short", "unsigned",
                                        "signed", "size_t", "wchar_t", "boolean", "byte", "qsizetype", "qreal",
                                        "uint", "qint64", "quint64", "qint32", "quint32", "ssize_t", "ptrdiff_t"};
    return words;
}

const QSet<QString> &reservedNames()
{
    static const QSet<QString> words = {
        "class", "new", "default", "delete", "operator", "template", "namespace", "int", "char", "bool", "float",
        "double", "long", "short", "union", "register", "this", "switch", "case", "do", "for", "if", "else",
        "while", "return", "enum", "struct", "goto", "try", "catch", "throw", "public", "private", "protected",
        "virtual", "static", "const", "auto", "void", "package", "import", "interface", "final", "boolean",
        "byte", "native", "super", "volatile", "transient", "and", "or", "not", "export", "module", "object"};
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
        if ((current.isUpper() && previous.isLower()) || (current.isUpper() && previous.isUpper() && next)
            || (current.isDigit() != previous.isDigit() && current.isLetter())) {
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
    for (const QLatin1String ending : {QLatin1String("s"), QLatin1String("x"), QLatin1String("z"), QLatin1String("ch"),
                                       QLatin1String("sh")}) {
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

} // namespace

bool NameSuggestionProvider::handles(const CompletionContext &context) const
{
    const QString &language = context.languageId;
    return context.triggerCharacter.isNull()
        && (language == QLatin1String("c") || language == QLatin1String("cpp") || isJava(language));
}

QString NameSuggestionProvider::declaredTypeAt(const QByteArray &text, int position, const QString &languageId)
{
    int i = position;
    bool separated = false;
    while (i > 0 && (text[i - 1] == ' ' || text[i - 1] == '\t' || text[i - 1] == '*' || text[i - 1] == '&')) {
        separated = true;
        --i;
    }
    if (!separated || i == 0)
        return {};
    const int end = i;
    // The type, read backwards: names, "::", ".", "<...>", "[]".
    while (i > 0) {
        const char c = text[i - 1];
        if (CodeScan::isIdentifierByte(c)) {
            --i;
        } else if (c == '>') {
            int depth = 0;
            int k = i - 1;
            for (; k >= 0; --k) {
                if (text[k] == '>')
                    ++depth;
                else if (text[k] == '<' && --depth == 0)
                    break;
                else if (text[k] == ';' || text[k] == '{' || text[k] == '}' || text[k] == '\n')
                    return {};
            }
            if (k <= 0)
                return {};
            i = k;
        } else if (c == ']' && i >= 2 && text[i - 2] == '[') {
            i -= 2;
        } else if (c == ':' && i >= 2 && text[i - 2] == ':') {
            i -= 2;
        } else if (c == '.' && isJava(languageId)) {
            --i;
        } else {
            break;
        }
    }
    const int typeStart = i;
    if (typeStart == end || (text[typeStart] >= '0' && text[typeStart] <= '9'))
        return {};
    // Before it: qualifiers, then the start of a statement or a parameter.
    int before = typeStart;
    while (true) {
        while (before > 0 && (text[before - 1] == ' ' || text[before - 1] == '\t'))
            --before;
        int wordStart = before;
        while (wordStart > 0 && CodeScan::isIdentifierByte(text[wordStart - 1]))
            --wordStart;
        if (wordStart == before || !qualifierWords().contains(text.mid(wordStart, before - wordStart)))
            break;
        before = wordStart;
    }
    if (before > 0 && !QByteArrayView("\n;{}(,").contains(text[before - 1])
        && !(text[before - 1] == ':' && !(before >= 2 && text[before - 2] == ':')))
        return {};
    const QString type = QString::fromUtf8(text.mid(typeStart, end - typeStart)).trimmed();
    return looksLikeType(type, languageId) ? type : QString();
}

QStringList NameSuggestionProvider::namesFor(const QString &type, const QString &languageId)
{
    QString cleaned = type;
    cleaned.remove(QRegularExpression(QStringLiteral("\\b(?:const|volatile|struct|enum|union|typename)\\b")));
    cleaned.remove(QLatin1Char('*'));
    cleaned.remove(QLatin1Char('&'));
    cleaned = cleaned.simplified();
    cleaned.remove(QLatin1Char(' '));
    QStringList names;
    const auto add = [&names](const QString &name) {
        if (!name.isEmpty() && !names.contains(name) && !reservedNames().contains(name))
            names.append(name);
    };
    if (cleaned.endsWith(QLatin1String("[]"))) {
        for (const QString &name : namesFor(cleaned.left(cleaned.size() - 2), languageId))
            add(plural(name));
        return names;
    }
    QStringList arguments;
    const QString base = splitTemplate(cleaned, &arguments);
    static const QSet<QString> containers = {
        "vector", "list", "deque", "set", "unordered_set", "multiset", "QList", "QVector", "QSet", "QStack",
        "QQueue", "span", "array", "List", "ArrayList", "LinkedList", "Set", "HashSet", "TreeSet", "Collection",
        "Iterable", "Queue", "Deque", "Stream", "initializer_list", "QVarLengthArray", "forward_list"};
    static const QSet<QString> wrappers = {"unique_ptr", "shared_ptr", "weak_ptr", "optional", "QPointer",
                                           "QSharedPointer", "QScopedPointer", "Optional", "atomic",
                                           "reference_wrapper", "QFuture", "Future", "CompletableFuture"};
    if (!arguments.isEmpty() && containers.contains(base)) {
        for (const QString &name : namesFor(arguments.first(), languageId))
            add(plural(name));
        add(lowerFirst(base));
        return names;
    }
    if (!arguments.isEmpty() && wrappers.contains(base))
        return namesFor(arguments.first(), languageId);

    if (primitives().contains(base)) {
        static const QHash<QString, QStringList> byType = {
            {"int", {"i", "n", "count"}}, {"long", {"l", "n"}}, {"short", {"s"}}, {"size_t", {"size", "n", "i"}},
            {"qsizetype", {"size", "n", "i"}}, {"char", {"c"}}, {"wchar_t", {"c"}}, {"bool", {"b", "enabled"}},
            {"boolean", {"b", "enabled"}}, {"float", {"f", "value"}}, {"double", {"d", "value"}},
            {"qreal", {"value"}}, {"byte", {"b"}}};
        for (const QString &name : byType.value(base, {QStringLiteral("value")}))
            add(name);
        return names;
    }
    static const QSet<QString> strings = {"string", "String", "QString", "wstring", "string_view", "QStringView"};
    if (strings.contains(base)) {
        add(QStringLiteral("text"));
        add(QStringLiteral("name"));
        add(QStringLiteral("s"));
        return names;
    }
    if (base.contains(QLatin1Char('_')) || base == base.toLower()) {
        // snake_case: "my_type_t" → "my_type", "type".
        QStringList words = base.split(QLatin1Char('_'), Qt::SkipEmptyParts);
        if (words.size() > 1 && words.last() == QLatin1String("t"))
            words.removeLast();
        for (qsizetype k = 0; k < words.size(); ++k)
            add(words.mid(k).join(QLatin1Char('_')).toLower());
        return names;
    }
    const QStringList words = camelWords(base);
    for (qsizetype k = 0; k < words.size(); ++k) {
        QString name = lowerFirst(words[k]);
        for (qsizetype j = k + 1; j < words.size(); ++j)
            name += words[j];
        add(name);
    }
    if (names.isEmpty() && !base.isEmpty())
        add(isJava(languageId) ? QStringLiteral("a") + base : lowerFirst(base) + QLatin1Char('_'));
    return names;
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
