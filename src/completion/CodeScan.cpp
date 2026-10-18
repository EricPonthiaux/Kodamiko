#include "CodeScan.h"

#include <QRegularExpression>
#include <QSet>

#include <cctype>

namespace CodeScan {
namespace {

// How far a declaration is read: a header rarely spans more.
constexpr int maxDeclarationBytes = 4000;

const QSet<QString> &specifierWords()
{
    static const QSet<QString> words = {
        "virtual", "static",   "inline",    "explicit", "constexpr", "consteval", "friend",       "extern",
        "mutable", "public",   "private",   "protected", "abstract", "final",     "synchronized", "native",
        "default", "async",    "export",    "function", "override",  "transient", "Q_INVOKABLE",  "Q_SLOT",
        "Q_SIGNAL", "strictfp", "readonly", "declare",
    };
    return words;
}

// Words before "(" that make it no function: a control statement, an
// operator, an expression.
const QSet<QString> &notFunctionWords()
{
    static const QSet<QString> words = {
        "if", "for", "while", "switch", "catch", "return", "sizeof", "decltype", "alignof", "typeid",
        "static_assert", "new", "delete", "defined", "throw", "do", "else", "case", "co_return", "co_await",
        "co_yield", "await", "yield", "__attribute__", "__declspec", "alignas", "noexcept", "requires", "assert",
        "synchronized", "with", "elif", "typeof", "instanceof", "in", "of",
    };
    return words;
}

const QSet<QString> &typeWords()
{
    static const QSet<QString> words = {"int", "char", "long", "short", "double", "float", "bool", "unsigned",
                                        "signed", "void", "auto", "const", "volatile", "struct", "class",
                                        "enum", "typename", "boolean", "byte"};
    return words;
}

// Access labels, preprocessor lines and lone macros ("Q_OBJECT") before a
// declaration.
QString withoutLabels(const QString &text)
{
    static const QRegularExpression label(
        QStringLiteral("^\\s*(?:(?:public|protected|private)(?:\\s+(?:slots|Q_SLOTS))?|signals|Q_SIGNALS|"
                       "Q_SLOTS)\\s*:(?!:)"));
    static const QRegularExpression macro(QStringLiteral("^\\s*[A-Z][A-Z0-9_]*(?:\\([^()]*\\))?\\s*$"));
    QStringList kept;
    for (QString line : text.split(QLatin1Char('\n'))) {
        while (true) {
            const QRegularExpressionMatch match = label.match(line);
            if (!match.hasMatch())
                break;
            line = line.mid(match.capturedLength());
        }
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')) || macro.match(trimmed).hasMatch())
            continue;
        kept.append(line);
    }
    return kept.join(QLatin1Char('\n')).trimmed();
}

// Position of the ">" closing the "<" at `open` in `text`, -1 if none.
int angleCloser(const QString &text, int open)
{
    int depth = 0;
    for (int i = open; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == u'<') {
            ++depth;
        } else if (c == u'>') {
            if (--depth == 0)
                return i;
        } else if (c == u';' || c == u'{' || c == u'}') {
            return -1;
        }
    }
    return -1;
}

// The closer of the bracket at `open` in `text` (a QString), -1 if none.
int closerIn(const QString &text, int open)
{
    const QChar opener = text[open];
    const QChar closer = opener == u'(' ? u')' : opener == u'[' ? u']' : opener == u'{' ? u'}' : u'>';
    int depth = 0;
    for (int i = open; i < text.size(); ++i) {
        if (text[i] == opener)
            ++depth;
        else if (text[i] == closer && --depth == 0)
            return i;
    }
    return -1;
}

QString simplifiedType(const QString &type)
{
    return type.simplified();
}

} // namespace

bool isIdentifierByte(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$'
        || static_cast<unsigned char>(c) >= 0x80;
}

QByteArray blank(const QByteArray &text)
{
    QByteArray out = text;
    const int size = int(text.size());
    int i = 0;
    const auto clear = [&out](int from, int to) {
        for (int k = from; k < to; ++k) {
            if (out[k] != '\n')
                out[k] = ' ';
        }
    };
    while (i < size) {
        const char c = text[i];
        const char next = i + 1 < size ? text[i + 1] : '\0';
        if (c == '/' && next == '/') {
            int end = i;
            while (end < size && text[end] != '\n')
                ++end;
            clear(i, end);
            i = end;
        } else if (c == '/' && next == '*') {
            int end = int(text.indexOf("*/", i + 2));
            end = end < 0 ? size : end + 2;
            clear(i, end);
            i = end;
        } else if (c == '"' && i > 0 && text[i - 1] == 'R' && (i < 2 || !isIdentifierByte(text[i - 2]) || text[i - 2] == '8'
                                                                 || text[i - 2] == 'L' || text[i - 2] == 'u'
                                                                 || text[i - 2] == 'U')) {
            // R"delimiter( ... )delimiter"
            const int paren = int(text.indexOf('(', i));
            if (paren < 0 || paren - i > 17) {
                ++i;
                continue;
            }
            const QByteArray close = ')' + text.mid(i + 1, paren - i - 1) + '"';
            int end = int(text.indexOf(close, paren));
            end = end < 0 ? size : end + int(close.size());
            clear(i + 1, end - 1);
            i = end;
        } else if (c == '"' || c == '`' || (c == '\'' && !(i > 0 && std::isxdigit(static_cast<unsigned char>(text[i - 1]))
                                                           && std::isxdigit(static_cast<unsigned char>(next))))) {
            // A string, a template literal or a character; "1'000" is a number.
            int end = i + 1;
            while (end < size && text[end] != c) {
                if (text[end] == '\\')
                    ++end;
                else if (text[end] == '\n' && c != '`')
                    break; // unterminated: the line ends it
                ++end;
            }
            clear(i + 1, std::min(end, size));
            i = end + 1;
        } else {
            ++i;
        }
    }
    return out;
}

int closerOf(const QByteArray &blanked, int open)
{
    if (open < 0 || open >= blanked.size())
        return -1;
    const char opener = blanked[open];
    if (opener == '<') {
        int depth = 0;
        for (int i = open; i < blanked.size(); ++i) {
            const char c = blanked[i];
            if (c == '<') {
                ++depth;
            } else if (c == '>') {
                if (--depth == 0)
                    return i;
            } else if (c == ';' || c == '{' || c == '}') {
                return -1;
            }
        }
        return -1;
    }
    QByteArray pending;
    for (int i = open; i < blanked.size(); ++i) {
        const char c = blanked[i];
        if (c == '(' || c == '[' || c == '{') {
            pending.append(c == '(' ? ')' : c == '[' ? ']' : '}');
        } else if (c == ')' || c == ']' || c == '}') {
            if (pending.isEmpty() || pending.back() != c)
                return -1;
            pending.chop(1);
            if (pending.isEmpty())
                return i;
        }
    }
    return -1;
}

int enclosingBrace(const QByteArray &blanked, int position)
{
    int depth = 0;
    for (int i = std::min<int>(position, int(blanked.size())) - 1; i >= 0; --i) {
        const char c = blanked[i];
        if (c == '}') {
            ++depth;
        } else if (c == '{') {
            if (depth == 0)
                return i;
            --depth;
        }
    }
    return -1;
}

int enclosingParenthesis(const QByteArray &blanked, int position)
{
    int depth = 0;
    for (int i = std::min<int>(position, int(blanked.size())) - 1; i >= 0; --i) {
        const char c = blanked[i];
        if (c == ')' || c == ']') {
            ++depth;
        } else if (c == '(' || c == '[') {
            if (depth == 0)
                return c == '(' ? i : -1;
            --depth;
        } else if (depth == 0 && (c == ';' || c == '{' || c == '}')) {
            return -1;
        }
    }
    return -1;
}

int headerStart(const QByteArray &blanked, int brace)
{
    int depth = 0;
    int start = 0;
    for (int i = brace - 1; i >= 0; --i) {
        const char c = blanked[i];
        if (c == ')') {
            ++depth;
        } else if (c == '(') {
            --depth;
        } else if (depth <= 0 && (c == ';' || c == '{' || c == '}')) {
            start = i + 1;
            break;
        }
    }
    // Preprocessor lines before it belong to no declaration.
    while (start < brace) {
        int lineEnd = int(blanked.indexOf('\n', start));
        if (lineEnd < 0 || lineEnd >= brace)
            break;
        const QByteArray line = blanked.mid(start, lineEnd - start).trimmed();
        if (!line.isEmpty() && !line.startsWith('#'))
            break;
        start = lineEnd + 1;
    }
    return start;
}

bool startsStatement(const QByteArray &text, int at, bool labels)
{
    int i = at - 1;
    while (i >= 0 && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r'))
        --i;
    if (i < 0 || QByteArrayView("\n{};").contains(text[i]))
        return true;
    return labels && text[i] == ':' && !(i > 0 && text[i - 1] == ':');
}

bool inModulePath(const QByteArray &text, int position)
{
    if (position <= 0 || position > text.size())
        return false;
    const int lineStart = int(text.lastIndexOf('\n', position - 1)) + 1;
    static const QRegularExpression opened(
        QStringLiteral("(?:\\bfrom|\\bimport|\\b(?:require|import)\\s*\\()\\s*['\"][^'\"]*$"));
    return opened.match(QString::fromUtf8(text.mid(lineStart, position - lineStart))).hasMatch();
}

QStringList splitTopLevel(const QString &text, QChar separator)
{
    QStringList parts;
    int depth = 0;
    int angle = 0;
    int start = 0;
    QChar quote;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (!quote.isNull()) {
            if (c == u'\\')
                ++i;
            else if (c == quote)
                quote = QChar();
            continue;
        }
        if (c == u'"' || c == u'\'' || c == u'`') {
            quote = c;
        } else if (c == u'(' || c == u'[' || c == u'{') {
            ++depth;
        } else if (c == u')' || c == u']' || c == u'}') {
            --depth;
        } else if (c == u'<') {
            ++angle;
        } else if (c == u'>' && angle > 0 && !(i > 0 && text[i - 1] == u'-')) {
            --angle;
        } else if (c == separator && depth == 0 && angle == 0) {
            parts.append(text.mid(start, i - start));
            start = i + 1;
        }
    }
    parts.append(text.mid(start));
    return parts;
}

Parameter parseParameter(const QString &text, bool typed)
{
    Parameter parameter;
    QString declaration = text.trimmed();
    // The default value: after the first "=" outside brackets.
    int depth = 0;
    for (int i = 0; i < declaration.size(); ++i) {
        const QChar c = declaration[i];
        if (c == u'(' || c == u'[' || c == u'{' || c == u'<')
            ++depth;
        else if (c == u')' || c == u']' || c == u'}' || c == u'>')
            --depth;
        else if (c == u'=' && depth == 0 && (i + 1 >= declaration.size() || declaration[i + 1] != u'=')) {
            parameter.defaultValue = declaration.mid(i + 1).trimmed();
            declaration = declaration.left(i).trimmed();
            break;
        }
    }
    parameter.text = declaration;
    if (!typed) {
        // JavaScript and TypeScript: "name", "name?: Type", "...rest".
        QString name = declaration;
        const int colon = int(name.indexOf(QLatin1Char(':')));
        if (colon >= 0 && !name.startsWith(QLatin1Char('{')) && !name.startsWith(QLatin1Char('['))) {
            parameter.type = name.mid(colon + 1).trimmed();
            name = name.left(colon);
        }
        name = name.trimmed();
        if (name.startsWith(QLatin1String("...")))
            name = name.mid(3);
        if (name.endsWith(QLatin1Char('?')))
            name.chop(1);
        // TypeScript's parameter properties: "private readonly name".
        static const QRegularExpression modifiers(QStringLiteral("^(?:(?:public|private|protected|readonly)\\s+)+"));
        name.remove(modifiers);
        parameter.name = name.trimmed();
        return parameter;
    }
    // A function pointer: "void (*callback)(int)".
    static const QRegularExpression pointer(QStringLiteral("\\(\\s*[*&^]\\s*([A-Za-z_]\\w*)\\s*\\)"));
    if (const QRegularExpressionMatch match = pointer.match(declaration); match.hasMatch()) {
        parameter.name = match.captured(1);
        parameter.type = declaration;
        parameter.type.remove(match.capturedStart(1), match.capturedLength(1));
        return parameter;
    }
    QString rest = declaration;
    QString arraySuffix;
    while (rest.endsWith(QLatin1Char(']'))) {
        const int open = int(rest.lastIndexOf(QLatin1Char('[')));
        if (open < 0)
            break;
        arraySuffix.prepend(rest.mid(open));
        rest = rest.left(open).trimmed();
    }
    int end = int(rest.size());
    int start = end;
    while (start > 0 && (rest[start - 1].isLetterOrNumber() || rest[start - 1] == u'_' || rest[start - 1] == u'$'))
        --start;
    const QString name = rest.mid(start, end - start);
    const QString before = rest.left(start).trimmed();
    // Words that need a type after them; "unsigned count", "long amount"
    // are named.
    static const QSet<QString> qualifiersOnly = {"const", "volatile", "struct", "class", "enum", "typename", "final"};
    if (name.isEmpty() || before.isEmpty() || typeWords().contains(name) || qualifiersOnly.contains(before)
        || before.endsWith(QLatin1String("::"))) {
        parameter.type = declaration; // a type alone: "int", "const Foo &"
        return parameter;
    }
    parameter.name = name;
    parameter.type = simplifiedType(before + arraySuffix);
    return parameter;
}

QList<Parameter> Function::parameterList(bool typed) const
{
    QList<Parameter> result;
    const QString inside = parameters.trimmed();
    if (inside.isEmpty() || inside == QLatin1String("void"))
        return result;
    for (const QString &part : splitTopLevel(inside)) {
        if (!part.trimmed().isEmpty())
            result.append(parseParameter(part, typed));
    }
    return result;
}

QString Function::qualifiers() const
{
    QString result = trailing;
    static const QRegularExpression removed(
        QStringLiteral("\\b(?:override|final|Q_DECL_OVERRIDE|Q_DECL_FINAL)\\b|=\\s*(?:0|default|delete)\\s*$"));
    result.remove(removed);
    return result.simplified();
}

bool Function::returnsValue() const
{
    const QString type = returnType.simplified();
    return !type.isEmpty() && type != QLatin1String("void") && type != QLatin1String("Unit");
}

Function parseFunction(const QString &header)
{
    Function function;
    QString text = withoutLabels(header);
    // "template <typename T>" before it.
    static const QRegularExpression templateHead(QStringLiteral("^\\s*template\\s*<"));
    if (const QRegularExpressionMatch match = templateHead.match(text); match.hasMatch()) {
        const int open = int(match.capturedEnd()) - 1;
        const int close = angleCloser(text, open);
        if (close < 0)
            return function;
        function.templateParameters = text.mid(open + 1, close - open - 1).simplified();
        text = text.mid(close + 1).trimmed();
    }
    // Annotations ("@Override") and attributes ("[[nodiscard]]").
    static const QRegularExpression annotation(QStringLiteral("@[A-Za-z_][\\w.]*(?:\\([^()]*\\))?"));
    for (QRegularExpressionMatchIterator it = annotation.globalMatch(text); it.hasNext();)
        function.specifiers.append(it.next().captured(0));
    text.remove(annotation);
    static const QRegularExpression attribute(QStringLiteral("\\[\\[[^\\]]*\\]\\]"));
    text.remove(attribute);
    text = text.trimmed();

    // The parameter list: the first "(" outside template arguments that
    // follows a name.
    int open = -1;
    int angle = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == u'<' && !(i > 0 && text[i - 1] == u'<') && !(i + 1 < text.size() && text[i + 1] == u'<')) {
            ++angle;
        } else if (c == u'>' && angle > 0 && !(i > 0 && text[i - 1] == u'-')) {
            --angle;
        } else if (c == u'(' && angle == 0) {
            open = i;
            break;
        } else if (c == u'=' || c == u';' || c == u'{') {
            break;
        }
    }
    if (open <= 0)
        return function;
    const int close = closerIn(text, open);
    if (close < 0)
        return function;

    QString before = text.left(open).trimmed();
    QString name;
    static const QRegularExpression operatorName(QStringLiteral("(operator\\s*(?:\\(\\)|[^\\s\\w(]+|\\w[\\w\\s:]*?))\\s*$"));
    if (const QRegularExpressionMatch match = operatorName.match(before); match.hasMatch()) {
        name = match.captured(1).simplified();
        before = before.left(match.capturedStart(1));
    } else {
        int start = int(before.size());
        while (start > 0 && (before[start - 1].isLetterOrNumber() || before[start - 1] == u'_' || before[start - 1] == u'$'))
            --start;
        if (start > 0 && before[start - 1] == u'~')
            --start;
        name = before.mid(start);
        before = before.left(start);
    }
    if (name.isEmpty() || notFunctionWords().contains(name) || name[0].isDigit())
        return function;
    // "Foo::bar", "Foo<T>::bar": the scope.
    before = before.trimmed();
    while (before.endsWith(QLatin1String("::"))) {
        QString scopePart;
        before.chop(2);
        before = before.trimmed();
        if (before.endsWith(QLatin1Char('>'))) {
            int depth = 0;
            int k = int(before.size()) - 1;
            for (; k >= 0; --k) {
                if (before[k] == u'>')
                    ++depth;
                else if (before[k] == u'<' && --depth == 0)
                    break;
            }
            if (k < 0)
                return function;
            scopePart = before.mid(k);
            before = before.left(k);
        }
        int start = int(before.size());
        while (start > 0 && (before[start - 1].isLetterOrNumber() || before[start - 1] == u'_'))
            --start;
        scopePart.prepend(before.mid(start));
        before = before.left(start).trimmed();
        function.scope = function.scope.isEmpty() ? scopePart : scopePart + QStringLiteral("::") + function.scope;
    }
    // An expression, not a declaration: "x = f(y)", "a.f(b)", "return f(x)".
    static const QRegularExpression expression(QStringLiteral("[=+\\-/!%^|?;(){}\"]|\\.(?!\\.\\.)"));
    QStringList words = before.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const QString &word : words) {
        if (notFunctionWords().contains(word))
            return function;
    }
    QString checked = before;
    static const QRegularExpression javaQualified(QStringLiteral("\\b[a-z]\\w*\\.(?=[A-Za-z])"));
    checked.remove(javaQualified); // "java.util.List"
    checked.remove(QStringLiteral("->"));
    if (expression.match(checked).hasMatch())
        return function;
    QStringList kept;
    for (const QString &word : words) {
        if (specifierWords().contains(word) || word.startsWith(QLatin1String("Q_DECL_")))
            function.specifiers.append(word);
        else
            kept.append(word);
    }
    function.returnType = simplifiedType(kept.join(QLatin1Char(' ')));
    function.name = name;
    function.parameters = text.mid(open + 1, close - open - 1).trimmed();
    function.trailing = text.mid(close + 1).simplified();
    // "auto f() -> int".
    if (const int arrow = int(function.trailing.indexOf(QLatin1String("->"))); arrow >= 0) {
        QString type = function.trailing.mid(arrow + 2);
        static const QRegularExpression tail(QStringLiteral("\\b(?:override|final)\\b.*$|=.*$"));
        type.remove(tail);
        if (function.returnType == QLatin1String("auto") || function.returnType.isEmpty())
            function.returnType = type.trimmed();
    }
    static const QRegularExpression pure(QStringLiteral("=\\s*0\\s*$"));
    static const QRegularExpression deleted(QStringLiteral("=\\s*(?:default|delete)\\s*$"));
    function.pure = pure.match(function.trailing).hasMatch() || function.specifiers.contains(QStringLiteral("abstract"));
    function.deleted = deleted.match(function.trailing).hasMatch();
    return function;
}

QString nextDeclaration(const QByteArray &text, int position)
{
    const int end = std::min<int>(int(text.size()), position + maxDeclarationBytes);
    const QByteArray part = text.mid(position, end - position);
    const QByteArray blanked = blank(part);
    int depth = 0;
    for (int i = 0; i < blanked.size(); ++i) {
        const char c = blanked[i];
        if (c == '(' || c == '[') {
            ++depth;
        } else if (c == ')' || c == ']') {
            --depth;
        } else if (depth == 0 && (c == '{' || c == ';')) {
            return QString::fromUtf8(blanked.left(i)).simplified();
        } else if (depth == 0 && c == '}') {
            return {}; // the end of the block: nothing follows
        }
    }
    return {};
}

QList<Declaration> declarationsIn(const QByteArray &blanked, int open)
{
    QList<Declaration> result;
    const int close = open < 0 ? -1 : closerOf(blanked, open);
    const int end = close < 0 ? int(blanked.size()) : close;
    static const QRegularExpression label(
        QStringLiteral("(?:^|\\n)\\s*((?:public|protected|private)(?:\\s+(?:slots|Q_SLOTS))?|signals|Q_SIGNALS|Q_SLOTS)"
                       "\\s*:(?!:)"));
    QString access;
    int depth = 0;
    int start = open + 1;
    const auto add = [&](int from, int to, bool hasBody) {
        const QString raw = QString::fromUtf8(blanked.mid(from, to - from));
        for (QRegularExpressionMatchIterator it = label.globalMatch(raw); it.hasNext();)
            access = it.next().captured(1).simplified();
        const QString declaration = withoutLabels(raw);
        if (!declaration.isEmpty())
            result.append({declaration, hasBody, from, hasBody ? to : -1, access});
    };
    for (int i = open + 1; i < end; ++i) {
        const char c = blanked[i];
        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
        } else if (depth == 0 && c == ';') {
            add(start, i, false);
            start = i + 1;
        } else if (depth == 0 && c == '{') {
            const int bodyEnd = closerOf(blanked, i);
            if (bodyEnd < 0)
                break;
            add(start, i, true);
            // "struct Inner {...};", "int x{0};": the ";" ends it too.
            int after = bodyEnd + 1;
            while (after < end && (blanked[after] == ' ' || blanked[after] == '\n' || blanked[after] == '\t'))
                ++after;
            start = after < end && blanked[after] == ';' ? after + 1 : bodyEnd + 1;
            i = start - 1;
        }
    }
    if (start < end)
        add(start, end, false); // a label or a declaration being typed
    return result;
}

ClassHeader classAt(const QByteArray &blanked, int brace)
{
    ClassHeader header;
    if (brace < 0)
        return header;
    QString source = withoutLabels(QString::fromUtf8(blanked.mid(headerStart(blanked, brace),
                                                                 brace - headerStart(blanked, brace))));
    static const QRegularExpression templateHead(QStringLiteral("^\\s*template\\s*<"));
    if (const QRegularExpressionMatch match = templateHead.match(source); match.hasMatch()) {
        const int close = angleCloser(source, int(match.capturedEnd()) - 1);
        if (close < 0)
            return header;
        source = source.mid(close + 1);
    }
    static const QRegularExpression keyword(
        QStringLiteral("(?:^|[\\s;])(?:(?:public|private|protected|abstract|final|static|sealed|export|default|"
                       "declare)\\s+)*(class|struct|interface|enum|record|union)\\b(?:\\s+(?:class|struct)\\b)?"));
    const QRegularExpressionMatch match = keyword.match(source);
    if (!match.hasMatch())
        return header;
    header.keyword = match.captured(1);
    QString rest = source.mid(match.capturedEnd());
    // Up to the bases.
    static const QRegularExpression basesStart(QStringLiteral(":(?!:)|\\bextends\\b|\\bimplements\\b"));
    const QRegularExpressionMatch bases = basesStart.match(rest);
    QString names = bases.hasMatch() ? rest.left(bases.capturedStart()) : rest;
    // Template arguments of a specialization, Java's generics.
    while (true) {
        const int angleOpen = int(names.indexOf(QLatin1Char('<')));
        if (angleOpen < 0)
            break;
        const int angleClose = angleCloser(names, angleOpen);
        names = angleClose < 0 ? names.left(angleOpen) : names.left(angleOpen) + names.mid(angleClose + 1);
    }
    static const QSet<QString> skipped = {"final", "sealed", "abstract", "alignas"};
    QStringList words;
    for (const QString &word : names.split(QRegularExpression(QStringLiteral("[^\\w$]+")), Qt::SkipEmptyParts)) {
        if (!skipped.contains(word))
            words.append(word);
    }
    if (words.isEmpty())
        return header;
    // "class EXPORT_MACRO Name": the last word is the name.
    header.name = words.last();
    if (!bases.hasMatch())
        return header;
    QString baseText = rest.mid(bases.capturedStart());
    baseText.replace(QRegularExpression(QStringLiteral("^:|\\bextends\\b|\\bimplements\\b")), QStringLiteral(","));
    baseText.replace(QRegularExpression(QStringLiteral("\\bextends\\b|\\bimplements\\b")), QStringLiteral(","));
    for (QString base : splitTopLevel(baseText)) {
        base.remove(QRegularExpression(QStringLiteral("\\b(?:public|protected|private|virtual)\\b")));
        const int angleOpen = int(base.indexOf(QLatin1Char('<')));
        if (angleOpen >= 0)
            base = base.left(angleOpen);
        base = base.trimmed();
        // "ns::Base", "pkg.Base": the name alone.
        if (const qsizetype scope = base.lastIndexOf(QLatin1String("::")); scope >= 0)
            base = base.mid(scope + 2);
        if (const qsizetype dot = base.lastIndexOf(QLatin1Char('.')); dot >= 0)
            base = base.mid(dot + 1);
        if (!base.isEmpty())
            header.bases.append(base);
    }
    return header;
}

int classBody(const QByteArray &blanked, const QString &name)
{
    const QRegularExpression declaration(
        QStringLiteral("\\b(?:class|struct|interface)\\s+(?:[A-Z_][A-Z0-9_]*\\s+)*%1\\b[^;{()]*\\{")
            .arg(QRegularExpression::escape(name)));
    const QString source = QString::fromUtf8(blanked);
    const QRegularExpressionMatch match = declaration.match(source);
    if (!match.hasMatch())
        return -1;
    // Back to bytes: the text before the match may hold non-ASCII.
    return int(source.left(match.capturedEnd() - 1).toUtf8().size());
}

QString lineIndent(const QByteArray &text, int at)
{
    int start = at;
    while (start > 0 && text[start - 1] != '\n')
        --start;
    int end = start;
    while (end < text.size() && (text[end] == ' ' || text[end] == '\t'))
        ++end;
    return QString::fromUtf8(text.mid(start, end - start));
}

QString indentUnit(const QByteArray &text)
{
    int tabs = 0;
    int spaced = 0;
    int smallest = 0;
    const qsizetype end = std::min<qsizetype>(text.size(), 200000);
    qsizetype i = 0;
    while (i < end) {
        if (text[i] == '\t') {
            ++tabs;
        } else if (text[i] == ' ') {
            qsizetype j = i;
            while (j < end && text[j] == ' ')
                ++j;
            const int width = int(j - i);
            // Not a blank line, nor the " *" of a comment.
            if (j < end && text[j] != '\n' && text[j] != '*' && width >= 2) {
                ++spaced;
                smallest = smallest == 0 ? width : std::min(smallest, width);
            }
        }
        while (i < end && text[i] != '\n')
            ++i;
        ++i;
    }
    if (tabs > spaced)
        return QStringLiteral("\t");
    return QString(smallest >= 2 && smallest <= 8 ? smallest : 4, QLatin1Char(' '));
}

QString indented(const QString &body, const QString &indent, const QString &unit)
{
    QStringList lines = body.split(QLatin1Char('\n'));
    for (qsizetype i = 0; i < lines.size(); ++i) {
        lines[i].replace(QLatin1Char('\t'), unit);
        if (i > 0 && !lines[i].isEmpty())
            lines[i].prepend(indent);
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace CodeScan
