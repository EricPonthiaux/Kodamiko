#include "ExpectedType.h"

#include "CodeScan.h"

#include <QRegularExpression>
#include <QSet>

namespace ExpectedType {
namespace {

bool isUntyped(const QString &languageId)
{
    return languageId == QLatin1String("javascript") || languageId == QLatin1String("python");
}

bool isTypeScript(const QString &languageId)
{
    return languageId == QLatin1String("typescript") || languageId == QLatin1String("tsx");
}

// The identifier ending at `end` (exclusive) in `text`.
QByteArray wordBefore(const QByteArray &text, int end)
{
    int start = end;
    while (start > 0 && CodeScan::isIdentifierByte(text[start - 1]))
        --start;
    return text.mid(start, end - start);
}

int skipBlanksBack(const QByteArray &text, int i)
{
    while (i > 0 && (text[i - 1] == ' ' || text[i - 1] == '\t' || text[i - 1] == '\n' || text[i - 1] == '\r'))
        --i;
    return i;
}

const QSet<QString> &specifiers()
{
    static const QSet<QString> words = {"static",  "const",     "final",        "constexpr", "constinit", "inline",
                                        "mutable", "volatile",  "thread_local", "extern",    "register",  "public",
                                        "private", "protected", "transient",    "readonly"};
    return words;
}

// Words before a name that make it no declaration: "return x".
const QSet<QString> &notTypes()
{
    static const QSet<QString> words = {"return", "case",      "new",       "delete",   "throw",   "else",
                                        "goto",   "co_return", "co_yield",  "co_await", "sizeof",  "typeof",
                                        "await",  "yield",     "in",        "of",       "not",     "and",
                                        "or",     "using",     "namespace", "import",   "package", "do"};
    return words;
}

// "static const int" → "int"; empty when what is left is no type.
QString typeOf(const QString &declaration)
{
    QStringList words;
    // Keeps "*" and "&" with the words: "Widget *".
    for (const QString &word : declaration.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (!specifiers().contains(word))
            words.append(word);
    }
    if (words.isEmpty() || notTypes().contains(words.last()))
        return {};
    for (const QString &word : words) {
        if (notTypes().contains(word))
            return {};
    }
    const QString type = words.join(QLatin1Char(' '));
    static const QSet<QString> inferred = {"auto", "var", "let", "def", "auto *", "auto &", "const auto &"};
    if (inferred.contains(type) || type == QLatin1String("auto*") || type == QLatin1String("auto&"))
        return {};
    return type;
}

// The text of the declaration before `end` back to the start of its
// statement, or to the "(" or "," of a parameter list (commas of template
// arguments kept).
QString declarationBefore(const QByteArray &blanked, int end)
{
    int angle = 0;
    int i = end;
    while (i > 0) {
        const char c = blanked[i - 1];
        if (c == '>') {
            ++angle;
        } else if (c == '<') {
            if (angle == 0)
                break;
            --angle;
        } else if (c == ';' || c == '{' || c == '}' || c == '(' || c == ')' || c == '\n' || (c == ',' && angle == 0) ||
                   (c == ':' && !(i >= 2 && blanked[i - 2] == ':') && !(i < blanked.size() && blanked[i] == ':'))) {
            break;
        }
        --i;
    }
    return QString::fromUtf8(blanked.mid(i, end - i)).trimmed();
}

// Whether `declaration` (before a name) reads as a type: no operator in
// it.
bool looksLikeType(const QString &declaration)
{
    if (declaration.isEmpty())
        return false;
    const QChar last = declaration.back();
    if (!(last.isLetterOrNumber() || last == u'_' || last == u'>' || last == u'*' || last == u'&' || last == u']'))
        return false;
    static const QRegularExpression operators(QStringLiteral("[=+\\-/!%^|?\"'()]|\\.(?=\\s|$)"));
    return !operators.match(declaration).hasMatch();
}

// The declared type of `name`, from its last declaration before `before`.
QString declaredType(const QByteArray &blanked, const QByteArray &name, int before, const QString &languageId)
{
    if (name.isEmpty() || before < name.size())
        return {};
    for (int at = int(blanked.lastIndexOf(name, before - int(name.size()))); at >= 0;
         at = at > 0 ? int(blanked.lastIndexOf(name, at - 1)) : -1) {
        const int end = at + int(name.size());
        if ((at > 0 && CodeScan::isIdentifierByte(blanked[at - 1])) ||
            (end < blanked.size() && CodeScan::isIdentifierByte(blanked[end])))
            continue;
        int after = end;
        while (after < blanked.size() && (blanked[after] == ' ' || blanked[after] == '\t'))
            ++after;
        if (after >= blanked.size())
            continue;
        const char next = blanked[after];
        if (isTypeScript(languageId) && next == ':') { // "name: Type"
            int typeEnd = after + 1;
            while (typeEnd < blanked.size() && !QByteArrayView("=;,){\n").contains(blanked[typeEnd]))
                ++typeEnd;
            return QString::fromUtf8(blanked.mid(after + 1, typeEnd - after - 1)).trimmed();
        }
        if (!QByteArrayView("=;,)[{(:").contains(next))
            continue;
        const QString declaration = declarationBefore(blanked, at);
        if (!looksLikeType(declaration))
            continue;
        const QString type = typeOf(declaration);
        if (!type.isEmpty())
            return type;
    }
    return {};
}

// The return type of the function around `position`.
QString returnType(const QByteArray &blanked, int position, const QString &languageId)
{
    for (int brace = CodeScan::enclosingBrace(blanked, position); brace >= 0;
         brace = CodeScan::enclosingBrace(blanked, brace)) {
        const int start = CodeScan::headerStart(blanked, brace);
        const QString header = QString::fromUtf8(blanked.mid(start, brace - start)).simplified();
        // A lambda: its own return type, when written.
        static const QRegularExpression lambda(QStringLiteral("\\]\\s*\\([^)]*\\)[^{]*$"));
        if (lambda.match(header).hasMatch()) {
            const int arrow = int(header.lastIndexOf(QLatin1String("->")));
            return arrow >= 0 ? header.mid(arrow + 2).trimmed() : QString();
        }
        const CodeScan::Function function = CodeScan::parseFunction(header);
        if (function.name.isEmpty())
            continue; // a block of a statement: "if (x) {"
        if (isTypeScript(languageId) && function.trailing.startsWith(QLatin1Char(':')))
            return function.trailing.mid(1).trimmed();
        const QString type = function.returnType;
        return type == QLatin1String("auto") ? QString() : type;
    }
    return {};
}

bool isNumber(const QString &type)
{
    static const QSet<QString> numbers = {"int",
                                          "long",
                                          "short",
                                          "unsigned",
                                          "signed",
                                          "unsignedint",
                                          "signedint",
                                          "longint",
                                          "longlong",
                                          "unsignedlong",
                                          "unsignedlonglong",
                                          "unsignedshort",
                                          "longdouble",
                                          "float",
                                          "double",
                                          "size_t",
                                          "ssize_t",
                                          "ptrdiff_t",
                                          "qsizetype",
                                          "qint8",
                                          "qint16",
                                          "qint32",
                                          "qint64",
                                          "quint8",
                                          "quint16",
                                          "quint32",
                                          "quint64",
                                          "qreal",
                                          "uint",
                                          "ulong",
                                          "ushort",
                                          "byte",
                                          "Integer",
                                          "Long",
                                          "Short",
                                          "Double",
                                          "Float",
                                          "Byte",
                                          "number",
                                          "BigDecimal",
                                          "BigInteger",
                                          "intptr_t",
                                          "uintptr_t",
                                          "size_type",
                                          "difference_type"};
    static const QRegularExpression fixed(QStringLiteral("^u?int(?:_least|_fast)?(?:8|16|32|64)_t$"));
    return numbers.contains(type) || fixed.match(type).hasMatch();
}

bool isString(const QString &type)
{
    static const QSet<QString> strings = {
        "string",        "QString",           "String",       "char*",   "wchar_t*",  "string_view",    "QStringView",
        "QLatin1String", "QLatin1StringView", "CharSequence", "wstring", "u16string", "QAnyStringView", "str"};
    return strings.contains(type);
}

bool isBoolean(const QString &type)
{
    return type == QLatin1String("bool") || type == QLatin1String("boolean") || type == QLatin1String("Boolean");
}

QString withoutArguments(const QString &type)
{
    const int open = int(type.indexOf(QLatin1Char('<')));
    return open < 0 ? type : type.left(open);
}

} // namespace

QString at(const QByteArray &text, int position, const QString &languageId)
{
    if (languageId == QLatin1String("python"))
        return {};
    const QByteArray blanked = CodeScan::blank(text.left(position));
    const int i = skipBlanksBack(blanked, position);
    if (i <= 0)
        return {};
    const char c = blanked[i - 1];
    const QByteArray word = wordBefore(blanked, i);

    if (word == "return" || word == "co_return")
        return isUntyped(languageId) ? QString() : returnType(blanked, i, languageId);

    // A condition.
    if (c == '(') {
        const QByteArray keyword = wordBefore(blanked, skipBlanksBack(blanked, i - 1));
        if (keyword == "if" || keyword == "while")
            return QStringLiteral("bool");
    }
    if ((c == '!' && !(i < blanked.size() && blanked[i] == '=')) ||
        (i >= 2 && ((c == '&' && blanked[i - 2] == '&') || (c == '|' && blanked[i - 2] == '|'))))
        return QStringLiteral("bool");

    if (c != '=' || isUntyped(languageId))
        return {};
    // "a == |", "a != |": the type of a.
    const char before = i >= 2 ? blanked[i - 2] : '\0';
    int operatorStart = i - 1;
    if (before == '=' || before == '!') {
        operatorStart = i - 2;
    } else if (before == '<' || before == '>') {
        return {}; // "a <= |": a number, but which
    } else if (QByteArrayView("+-*/%&|^").contains(before)) {
        operatorStart = i - 2; // "a += |"
    }
    const int lhsEnd = skipBlanksBack(blanked, operatorStart);
    if (isTypeScript(languageId)) {
        // "let x: Foo = |".
        int statement = lhsEnd;
        while (statement > 0 && !QByteArrayView(";{}(\n").contains(blanked[statement - 1]))
            --statement;
        static const QRegularExpression annotated(QStringLiteral("[\\w$]\\s*\\??:\\s*(.+)$"));
        const QRegularExpressionMatch match =
            annotated.match(QString::fromUtf8(blanked.mid(statement, lhsEnd - statement)));
        if (match.hasMatch())
            return match.captured(1).trimmed();
        return declaredType(blanked, wordBefore(blanked, lhsEnd), lhsEnd, languageId);
    }
    const QString lhs = declarationBefore(blanked, lhsEnd);
    if (lhs.isEmpty())
        return {};
    // "Type name = |": a declaration.
    static const QRegularExpression declaration(
        QStringLiteral("^(.*?[\\w>\\]*&])\\s*\\b([A-Za-z_$][\\w$]*)\\s*(?:\\[[^\\]]*\\])?$"));
    if (const QRegularExpressionMatch match = declaration.match(lhs);
        match.hasMatch() && operatorStart == i - 1 && looksLikeType(match.captured(1))) {
        QString type = typeOf(match.captured(1));
        if (!type.isEmpty())
            return type;
        if (!match.captured(1).simplified().isEmpty())
            return {}; // "auto x = |"
    }
    // "name = |", "this->name = |", "a.b = |": the name's declaration.
    return declaredType(blanked, wordBefore(blanked, lhsEnd), lhsEnd, languageId);
}

QString ofParameter(const QString &label, const QString &languageId)
{
    if (isUntyped(languageId) || isTypeScript(languageId) || languageId == QLatin1String("python")) {
        const int colon = int(label.indexOf(QLatin1Char(':')));
        if (colon < 0)
            return {};
        QString type = label.mid(colon + 1);
        const int equals = int(type.indexOf(QLatin1Char('=')));
        return (equals >= 0 ? type.left(equals) : type).trimmed();
    }
    const CodeScan::Parameter parameter = CodeScan::parseParameter(label);
    return parameter.type.trimmed();
}

QString ofDetail(const QString &detail)
{
    const QString text = detail.trimmed();
    if (const int colon = int(text.lastIndexOf(QLatin1String(" : "))); colon >= 0)
        return text.mid(colon + 3).trimmed(); // jdtls: "Main.count : int"
    if (const int arrow = int(text.lastIndexOf(QLatin1String("->"))); arrow >= 0)
        return text.mid(arrow + 2).trimmed(); // pyright: "(x: int) -> str"
    if (text.startsWith(QLatin1Char('(')))
        return {}; // a signature alone
    return text;
}

QString normalized(const QString &type)
{
    QString result = type;
    static const QRegularExpression qualifiers(
        QStringLiteral("\\b(?:const|volatile|struct|class|enum|typename|final|mutable|constexpr|static|inline)\\b"));
    result.remove(qualifiers);
    result.remove(QLatin1Char('&'));
    static const QRegularExpression packages(QStringLiteral("\\b[a-z]\\w*\\.(?=[A-Za-z])"));
    result.remove(packages); // "java.lang.String"
    result.remove(QRegularExpression(QStringLiteral("\\s+")));
    result.remove(QStringLiteral("std::"));
    if (result.startsWith(QLatin1String("::")))
        result = result.mid(2);
    return result;
}

bool accepts(const QString &expected, const CompletionItem &item, bool afterNew)
{
    using Kind = CompletionItem::Kind;
    const QString wanted = normalized(expected);
    const QString wantedClass = withoutArguments(QString(wanted).remove(QLatin1Char('*')));
    if (afterNew) {
        const bool isClass = item.kind == Kind::Class || item.kind == Kind::Struct;
        return isClass && (wanted.isEmpty() || item.label == wantedClass);
    }
    switch (item.kind) {
    case Kind::Variable:
    case Kind::Field:
    case Kind::Property:
    case Kind::Constant:
    case Kind::Function:
    case Kind::Method:
        break;
    case Kind::Class:
    case Kind::Struct:
        // A constructor: "Foo f = Foo(...)".
        return !wanted.isEmpty() && item.label == wantedClass;
    default:
        return false;
    }
    if (wanted.isEmpty())
        return true;
    const QString type = normalized(item.type);
    if (type.isEmpty())
        return false;
    if (type == wanted)
        return true;
    if ((isNumber(type) && isNumber(wanted)) || (isString(type) && isString(wanted)) ||
        (isBoolean(type) && isBoolean(wanted)))
        return true;
    // "QList" and "QList<int>": one of them without its arguments.
    if (!type.contains(QLatin1Char('<')) || !wanted.contains(QLatin1Char('<')))
        return withoutArguments(type) == withoutArguments(wanted) &&
               type.endsWith(QLatin1Char('*')) == wanted.endsWith(QLatin1Char('*'));
    return false;
}

} // namespace ExpectedType
