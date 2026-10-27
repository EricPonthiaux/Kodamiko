// SPDX-License-Identifier: GPL-3.0-only

#include "CodeScan.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cctype>
#include <optional>

namespace CodeScan {
namespace {

// How far a declaration is read: a header rarely spans more.
constexpr int maxDeclarationBytes = 4000;

const QSet<QString> &specifierWords()
{
    static const QSet<QString> words = {
        "virtual",      "static",      "inline",  "explicit", "constexpr", "consteval", "friend",
        "extern",       "mutable",     "public",  "private",  "protected", "abstract",  "final",
        "synchronized", "native",      "default", "async",    "export",    "function",  "override",
        "transient",    "Q_INVOKABLE", "Q_SLOT",  "Q_SIGNAL", "strictfp",  "readonly",  "declare",
    };
    return words;
}

// Words before "(" that make it no function: a control statement, an
// operator, an expression.
const QSet<QString> &notFunctionWords()
{
    static const QSet<QString> words = {
        "if",       "for",          "while",         "switch",        "catch",     "return",     "sizeof",
        "decltype", "alignof",      "typeid",        "static_assert", "new",       "delete",     "defined",
        "throw",    "do",           "else",          "case",          "co_return", "co_await",   "co_yield",
        "await",    "yield",        "__attribute__", "__declspec",    "alignas",   "noexcept",   "requires",
        "assert",   "synchronized", "with",          "elif",          "typeof",    "instanceof", "in",
        "of",
    };
    return words;
}

const QSet<QString> &typeWords()
{
    static const QSet<QString> words = {"int",      "char",   "long",     "short",   "double", "float",    "bool",
                                        "unsigned", "signed", "void",     "auto",    "const",  "volatile", "struct",
                                        "class",    "enum",   "typename", "boolean", "byte"};
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

// ")" for "(", "]" for "[", "}" for "{", ">" for anything else.
char closingBracket(char opener)
{
    switch (opener) {
    case '(':
        return ')';
    case '[':
        return ']';
    case '{':
        return '}';
    default:
        return '>';
    }
}

// The closer of the bracket at `open` in `text` (a QString), -1 if none.
int closerIn(const QString &text, int open)
{
    const QChar opener = text[open];
    const QChar closer = QLatin1Char(closingBracket(opener.toLatin1()));
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

bool isNameCharacter(QChar c)
{
    return c.isLetterOrNumber() || c == u'_' || c == u'$';
}

// Whether the ">" at `i` is the end of an arrow: "->".
bool isArrowEnd(const QString &text, int i)
{
    return i > 0 && text[i - 1] == u'-';
}

bool isStatementEnd(char c)
{
    return c == ';' || c == '{' || c == '}';
}

// Spaces over `out[from, to)`, line breaks kept.
void clearRange(QByteArray &out, int from, int to)
{
    for (int k = from; k < to; ++k) {
        if (out[k] != '\n')
            out[k] = ' ';
    }
}

// What `blank` clears of a comment or literal starting at a position:
// [clearFrom, clearTo), the scan resuming at `next`.
struct BlankSpan {
    int clearFrom = 0;
    int clearTo = 0;
    int next = 0;
};

BlankSpan lineCommentAt(const QByteArray &text, int i)
{
    const int end = int(text.indexOf('\n', i));
    const int lineEnd = end < 0 ? int(text.size()) : end;
    return {i, lineEnd, lineEnd};
}

BlankSpan blockCommentAt(const QByteArray &text, int i)
{
    const int close = int(text.indexOf("*/", i + 2));
    const int end = close < 0 ? int(text.size()) : close + 2;
    return {i, end, end};
}

// The '"' of R"delimiter(...)delimiter", u8R"(...)", LR"(...)"...
bool isRawStringStart(const QByteArray &text, int i)
{
    if (text[i] != '"' || i == 0 || text[i - 1] != 'R')
        return false;
    if (i < 2 || !isIdentifierByte(text[i - 2]))
        return true;
    const char prefix = text[i - 2];
    return prefix == '8' || prefix == 'L' || prefix == 'u' || prefix == 'U';
}

// R"delimiter( ... )delimiter": what is between the quotes.
BlankSpan rawStringAt(const QByteArray &text, int i)
{
    constexpr int maxDelimiterBytes = 16;
    const int paren = int(text.indexOf('(', i));
    if (paren < 0 || paren - i > maxDelimiterBytes + 1)
        return {i, i, i + 1};
    const QByteArray close = ')' + text.mid(i + 1, paren - i - 1) + '"';
    const int found = int(text.indexOf(close, paren));
    const int end = found < 0 ? int(text.size()) : found + int(close.size());
    return {i + 1, end - 1, end};
}

// A string, a template literal or a character; "1'000" is a number.
bool isQuoteStart(const QByteArray &text, int i)
{
    const char c = text[i];
    if (c == '"' || c == '`')
        return true;
    if (c != '\'')
        return false;
    const char next = i + 1 < text.size() ? text[i + 1] : '\0';
    const bool isDigitSeparator = i > 0 && std::isxdigit(static_cast<unsigned char>(text[i - 1])) &&
                                  std::isxdigit(static_cast<unsigned char>(next));
    return !isDigitSeparator;
}

BlankSpan quotedAt(const QByteArray &text, int i)
{
    const char quote = text[i];
    const int size = int(text.size());
    int end = i + 1;
    while (end < size && text[end] != quote) {
        if (text[end] == '\\')
            ++end;
        else if (text[end] == '\n' && quote != '`')
            break; // unterminated: the line ends it
        ++end;
    }
    return {i + 1, std::min(end, size), end + 1};
}

BlankSpan blankSpanAt(const QByteArray &text, int i)
{
    const char c = text[i];
    const char next = i + 1 < text.size() ? text[i + 1] : '\0';
    if (c == '/' && next == '/')
        return lineCommentAt(text, i);
    if (c == '/' && next == '*')
        return blockCommentAt(text, i);
    if (isRawStringStart(text, i))
        return rawStringAt(text, i);
    if (isQuoteStart(text, i))
        return quotedAt(text, i);
    return {i, i, i + 1};
}

// In blanked text: the ">" closing the "<" at `open`, -1 if none.
int angleCloserIn(const QByteArray &blanked, int open)
{
    int depth = 0;
    for (int i = open; i < blanked.size(); ++i) {
        const char c = blanked[i];
        if (c == '<') {
            ++depth;
        } else if (c == '>') {
            if (--depth == 0)
                return i;
        } else if (isStatementEnd(c)) {
            return -1;
        }
    }
    return -1;
}

// In blanked text: the bracket closing the "(", "[" or "{" at `open`, -1
// if none or if a bracket in between is closed by another kind.
int bracketCloserIn(const QByteArray &blanked, int open)
{
    QByteArray pending;
    for (int i = open; i < blanked.size(); ++i) {
        const char c = blanked[i];
        if (c == '(' || c == '[' || c == '{') {
            pending.append(closingBracket(c));
            continue;
        }
        if (c != ')' && c != ']' && c != '}')
            continue;
        if (pending.isEmpty() || pending.back() != c)
            return -1;
        pending.chop(1);
        if (pending.isEmpty())
            return i;
    }
    return -1;
}

// The end of the block opened at `open` (-1: the whole text): its "}",
// or the end of the text.
int blockEnd(const QByteArray &blanked, int open)
{
    const int close = open < 0 ? -1 : closerOf(blanked, open);
    return close < 0 ? int(blanked.size()) : close;
}

// The access label `raw` leaves in force: its last one, else `current`.
QString lastAccessLabel(const QString &raw, const QString &current)
{
    static const QRegularExpression label(
        QStringLiteral("(?:^|\\n)\\s*((?:public|protected|private)(?:\\s+(?:slots|Q_SLOTS))?|signals|Q_SIGNALS|Q_SLOTS)"
                       "\\s*:(?!:)"));
    QString access = current;
    for (QRegularExpressionMatchIterator it = label.globalMatch(raw); it.hasNext();)
        access = it.next().captured(1).simplified();
    return access;
}

// Where the declaration after a body closed at `bodyEnd` starts: "struct
// Inner {...};", "int x{0};": the ";" ends it too.
int afterBody(const QByteArray &blanked, int bodyEnd, int end)
{
    int after = bodyEnd + 1;
    while (after < end && (blanked[after] == ' ' || blanked[after] == '\n' || blanked[after] == '\t'))
        ++after;
    const bool endsWithSemicolon = after < end && blanked[after] == ';';
    return endsWithSemicolon ? after + 1 : bodyEnd + 1;
}

// A declaration of a block in blanked text: [from, to), up to its ";"
// or its body's "{".
struct DeclarationRange {
    int from = 0;
    int to = 0;
    bool hasBody = false;
};

// The declarations of the block opened at `open` (-1: the whole text),
// labels included.
QList<DeclarationRange> declarationRanges(const QByteArray &blanked, int open)
{
    QList<DeclarationRange> ranges;
    const int end = blockEnd(blanked, open);
    int depth = 0;
    int start = open + 1;
    for (int i = open + 1; i < end; ++i) {
        const char c = blanked[i];
        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
        } else if (depth == 0 && c == ';') {
            ranges.append({start, i, false});
            start = i + 1;
        } else if (depth == 0 && c == '{') {
            const int bodyEnd = closerOf(blanked, i);
            if (bodyEnd < 0)
                break;
            ranges.append({start, i, true});
            start = afterBody(blanked, bodyEnd, end);
            i = start - 1;
        }
    }
    if (start < end)
        ranges.append({start, end, false}); // a label or a declaration being typed
    return ranges;
}

// The position of the "=" before a default value: the first outside
// brackets, -1 if none.
int defaultValueStart(const QString &declaration)
{
    int depth = 0;
    for (int i = 0; i < declaration.size(); ++i) {
        const QChar c = declaration[i];
        if (c == u'(' || c == u'[' || c == u'{' || c == u'<') {
            ++depth;
        } else if (c == u')' || c == u']' || c == u'}' || c == u'>') {
            --depth;
        } else if (c == u'=' && depth == 0) {
            const bool isComparison = i + 1 < declaration.size() && declaration[i + 1] == u'=';
            if (!isComparison)
                return i;
        }
    }
    return -1;
}

// JavaScript and TypeScript: "name", "name?: Type", "...rest".
Parameter untypedParameter(const QString &declaration)
{
    Parameter parameter;
    QString name = declaration;
    const int colon = int(name.indexOf(QLatin1Char(':')));
    const bool isDestructuring = name.startsWith(QLatin1Char('{')) || name.startsWith(QLatin1Char('['));
    if (colon >= 0 && !isDestructuring) {
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

// "char *argv[]": "char *" and "[]".
struct ArraySplit {
    QString rest;
    QString suffix;
};

ArraySplit splitArraySuffix(const QString &declaration)
{
    ArraySplit split{declaration, QString()};
    while (split.rest.endsWith(QLatin1Char(']'))) {
        const int open = int(split.rest.lastIndexOf(QLatin1Char('[')));
        if (open < 0)
            break;
        split.suffix.prepend(split.rest.mid(open));
        split.rest = split.rest.left(open).trimmed();
    }
    return split;
}

// C, C++, Java: "const QString &name", "void (*callback)(int)".
Parameter typedParameter(const QString &declaration)
{
    Parameter parameter;
    // A function pointer: "void (*callback)(int)".
    static const QRegularExpression pointer(QStringLiteral("\\(\\s*[*&^]\\s*([A-Za-z_]\\w*)\\s*\\)"));
    if (const QRegularExpressionMatch match = pointer.match(declaration); match.hasMatch()) {
        parameter.name = match.captured(1);
        parameter.type = declaration;
        parameter.type.remove(match.capturedStart(1), match.capturedLength(1));
        return parameter;
    }
    const ArraySplit array = splitArraySuffix(declaration);
    const QString &rest = array.rest;
    const int end = int(rest.size());
    int start = end;
    while (start > 0 && isNameCharacter(rest[start - 1]))
        --start;
    const QString name = rest.mid(start, end - start);
    const QString before = rest.left(start).trimmed();
    // Words that need a type after them; "unsigned count", "long amount"
    // are named.
    static const QSet<QString> qualifiersOnly = {"const", "volatile", "struct", "class", "enum", "typename", "final"};
    const bool isTypeAlone = name.isEmpty() || before.isEmpty() || typeWords().contains(name) ||
                             qualifiersOnly.contains(before) || before.endsWith(QLatin1String("::"));
    if (isTypeAlone) {
        parameter.type = declaration; // a type alone: "int", "const Foo &"
        return parameter;
    }
    parameter.name = name;
    parameter.type = simplifiedType(before + array.suffix);
    return parameter;
}

// "template <typename T> int f()" split into "typename T" and "int f()".
struct TemplateHead {
    QString parameters;
    QString rest;
};

// Nullopt when the "<" is never closed; no parameters when `text` starts
// with no "template".
std::optional<TemplateHead> splitTemplateHead(const QString &text)
{
    static const QRegularExpression templateHead(QStringLiteral("^\\s*template\\s*<"));
    const QRegularExpressionMatch match = templateHead.match(text);
    if (!match.hasMatch())
        return TemplateHead{QString(), text};
    const int open = int(match.capturedEnd()) - 1;
    const int close = angleCloser(text, open);
    if (close < 0)
        return std::nullopt;
    return TemplateHead{text.mid(open + 1, close - open - 1).simplified(), text.mid(close + 1).trimmed()};
}

// Whether the "<" at `i` belongs to a "<<".
bool isShiftAt(const QString &text, int i)
{
    const bool afterLess = i > 0 && text[i - 1] == u'<';
    const bool beforeLess = i + 1 < text.size() && text[i + 1] == u'<';
    return afterLess || beforeLess;
}

// The parameter list: the first "(" outside template arguments that
// follows a name. -1 if none.
int parameterListOpen(const QString &text)
{
    int angle = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == u'<' && !isShiftAt(text, i)) {
            ++angle;
        } else if (c == u'>' && angle > 0 && !isArrowEnd(text, i)) {
            --angle;
        } else if (c == u'(' && angle == 0) {
            return i;
        } else if (c == u'=' || c == u';' || c == u'{') {
            return -1;
        }
    }
    return -1;
}

// What comes before a function's "(": its name and what precedes it.
struct NameSplit {
    QString name;
    QString before;
};

NameSplit splitName(const QString &before)
{
    static const QRegularExpression operatorName(
        QStringLiteral("(operator\\s*(?:\\(\\)|[^\\s\\w(]+|\\w[\\w\\s:]*?))\\s*$"));
    if (const QRegularExpressionMatch match = operatorName.match(before); match.hasMatch())
        return {match.captured(1).simplified(), before.left(match.capturedStart(1))};
    int start = int(before.size());
    while (start > 0 && isNameCharacter(before[start - 1]))
        --start;
    if (start > 0 && before[start - 1] == u'~')
        --start;
    return {before.mid(start), before.left(start)};
}

// Where the template arguments closed at the end of `text` open: "Foo<T>"
// gives 3. -1 if they never open.
int templateArgumentsStart(const QString &text)
{
    int depth = 0;
    for (int k = int(text.size()) - 1; k >= 0; --k) {
        if (text[k] == u'>')
            ++depth;
        else if (text[k] == u'<' && --depth == 0)
            return k;
    }
    return -1;
}

// "Foo<T>::Bar::" before a name: the scope "Foo<T>::Bar" and what
// precedes it.
struct ScopeSplit {
    QString scope;
    QString before;
};

// Nullopt when a template argument list is never opened.
std::optional<ScopeSplit> splitScope(const QString &text)
{
    ScopeSplit split{QString(), text};
    QString &before = split.before;
    while (before.endsWith(QLatin1String("::"))) {
        QString scopePart;
        before.chop(2);
        before = before.trimmed();
        if (before.endsWith(QLatin1Char('>'))) {
            const int k = templateArgumentsStart(before);
            if (k < 0)
                return std::nullopt;
            scopePart = before.mid(k);
            before = before.left(k);
        }
        int start = int(before.size());
        while (start > 0 && (before[start - 1].isLetterOrNumber() || before[start - 1] == u'_'))
            --start;
        scopePart.prepend(before.mid(start));
        before = before.left(start).trimmed();
        split.scope = split.scope.isEmpty() ? scopePart : scopePart + QStringLiteral("::") + split.scope;
    }
    return split;
}

// An expression, not a declaration, before the "(": "x = f(y)", "a.f(b)",
// "return f(x)".
bool isExpression(const QString &before, const QStringList &words)
{
    const bool hasStatementWord =
        std::ranges::any_of(words, [](const QString &word) { return notFunctionWords().contains(word); });
    if (hasStatementWord)
        return true;
    static const QRegularExpression expression(QStringLiteral("[=+\\-/!%^|?;(){}\"]|\\.(?!\\.\\.)"));
    static const QRegularExpression javaQualified(QStringLiteral("\\b[a-z]\\w*\\.(?=[A-Za-z])"));
    QString checked = before;
    checked.remove(javaQualified); // "java.util.List"
    checked.remove(QStringLiteral("->"));
    return expression.match(checked).hasMatch();
}

bool isFunctionName(const QString &name)
{
    return !name.isEmpty() && !notFunctionWords().contains(name) && !name[0].isDigit();
}

// The words before a function's name: its specifiers ("virtual",
// "Q_DECL_EXPORT"...) and those of its return type.
struct SpecifierSplit {
    QStringList specifiers;
    QStringList returnTypeWords;
};

SpecifierSplit splitSpecifiers(const QStringList &words)
{
    SpecifierSplit split;
    for (const QString &word : words) {
        if (specifierWords().contains(word) || word.startsWith(QLatin1String("Q_DECL_")))
            split.specifiers.append(word);
        else
            split.returnTypeWords.append(word);
    }
    return split;
}

// "auto f() -> int": "int", nullopt without an arrow.
std::optional<QString> trailingReturnType(const QString &trailing)
{
    const int arrow = int(trailing.indexOf(QLatin1String("->")));
    if (arrow < 0)
        return std::nullopt;
    QString type = trailing.mid(arrow + 2);
    static const QRegularExpression tail(QStringLiteral("\\b(?:override|final)\\b.*$|=.*$"));
    type.remove(tail);
    return type.trimmed();
}

// The header of the class whose body opens at `brace`, labels and
// template head left out. Nullopt when the template head is never closed.
std::optional<QString> classHeaderText(const QByteArray &blanked, int brace)
{
    const int start = headerStart(blanked, brace);
    const QString source = withoutLabels(QString::fromUtf8(blanked.mid(start, brace - start)));
    const std::optional<TemplateHead> head = splitTemplateHead(source);
    if (!head)
        return std::nullopt;
    return head->rest;
}

// Template arguments of a specialization, Java's generics: left out.
QString withoutAngleArguments(QString names)
{
    while (true) {
        const int angleOpen = int(names.indexOf(QLatin1Char('<')));
        if (angleOpen < 0)
            return names;
        const int angleClose = angleCloser(names, angleOpen);
        const QString after = angleClose < 0 ? QString() : names.mid(angleClose + 1);
        names = names.left(angleOpen) + after;
    }
}

// "class EXPORT_MACRO Name final": the last word that is no specifier.
QString classNameIn(const QString &names)
{
    static const QSet<QString> skipped = {"final", "sealed", "abstract", "alignas"};
    static const QRegularExpression separators(QStringLiteral("[^\\w$]+"));
    QString name;
    for (const QString &word : names.split(separators, Qt::SkipEmptyParts)) {
        if (!skipped.contains(word))
            name = word;
    }
    return name;
}

// "public ns::Base<T>", "pkg.Base": "Base".
QString baseName(QString base)
{
    static const QRegularExpression accessSpecifiers(QStringLiteral("\\b(?:public|protected|private|virtual)\\b"));
    base.remove(accessSpecifiers);
    const int angleOpen = int(base.indexOf(QLatin1Char('<')));
    if (angleOpen >= 0)
        base = base.left(angleOpen);
    base = base.trimmed();
    if (const qsizetype scope = base.lastIndexOf(QLatin1String("::")); scope >= 0)
        base = base.mid(scope + 2);
    if (const qsizetype dot = base.lastIndexOf(QLatin1Char('.')); dot >= 0)
        base = base.mid(dot + 1);
    return base;
}

// ": public A, B", "extends A implements B": "A", "B".
QStringList baseNamesIn(QString baseText)
{
    static const QRegularExpression firstBaseIntroducer(QStringLiteral("^:|\\bextends\\b|\\bimplements\\b"));
    static const QRegularExpression baseIntroducer(QStringLiteral("\\bextends\\b|\\bimplements\\b"));
    baseText.replace(firstBaseIntroducer, QStringLiteral(","));
    baseText.replace(baseIntroducer, QStringLiteral(","));
    QStringList bases;
    for (const QString &part : splitTopLevel(baseText)) {
        if (const QString base = baseName(part); !base.isEmpty())
            bases.append(base);
    }
    return bases;
}

// The width of the spaces indenting the line at `i`, 0 when it is not
// indented with spaces, blank, or the " *" of a comment.
int spaceIndentWidth(const QByteArray &text, qsizetype i, qsizetype end)
{
    if (text[i] != ' ')
        return 0;
    qsizetype j = i;
    while (j < end && text[j] == ' ')
        ++j;
    const int width = int(j - i);
    const bool startsCode = j < end && text[j] != '\n' && text[j] != '*';
    return startsCode && width >= 2 ? width : 0;
}

// Where the quote opened at `open` closes; the end of `text` when it
// never does.
int closingQuote(const QString &text, int open)
{
    const QChar quote = text[open];
    for (int i = open + 1; i < text.size(); ++i) {
        if (text[i] == u'\\')
            ++i;
        else if (text[i] == quote)
            return i;
    }
    return int(text.size());
}

int bracketDelta(QChar c)
{
    if (c == u'(' || c == u'[' || c == u'{')
        return 1;
    if (c == u')' || c == u']' || c == u'}')
        return -1;
    return 0;
}

// How the "<" or ">" at `i` changes the depth of template arguments.
int angleDelta(const QString &text, int i, int angle)
{
    if (text[i] == u'<')
        return 1;
    const bool closesArguments = text[i] == u'>' && angle > 0 && !isArrowEnd(text, i);
    return closesArguments ? -1 : 0;
}

} // namespace

bool isIdentifierByte(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' ||
           static_cast<unsigned char>(c) >= 0x80;
}

QByteArray blank(const QByteArray &text)
{
    QByteArray out = text;
    int i = 0;
    while (i < text.size()) {
        const BlankSpan span = blankSpanAt(text, i);
        clearRange(out, span.clearFrom, span.clearTo);
        i = span.next;
    }
    return out;
}

int closerOf(const QByteArray &blanked, int open)
{
    if (open < 0 || open >= blanked.size())
        return -1;
    if (blanked[open] == '<')
        return angleCloserIn(blanked, open);
    return bracketCloserIn(blanked, open);
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
        } else if (depth == 0 && isStatementEnd(c)) {
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
        const int lineEnd = int(blanked.indexOf('\n', start));
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
    const bool isScopeOperator = i > 0 && text[i - 1] == ':';
    return labels && text[i] == ':' && !isScopeOperator;
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
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == u'"' || c == u'\'' || c == u'`') {
            i = closingQuote(text, i);
            continue;
        }
        if (const int delta = bracketDelta(c); delta != 0) {
            depth += delta;
            continue;
        }
        if (const int delta = angleDelta(text, i, angle); delta != 0) {
            angle += delta;
            continue;
        }
        if (c == separator && depth == 0 && angle == 0) {
            parts.append(text.mid(start, i - start));
            start = i + 1;
        }
    }
    parts.append(text.mid(start));
    return parts;
}

Parameter parseParameter(const QString &text, bool typed)
{
    QString declaration = text.trimmed();
    QString defaultValue;
    if (const int equals = defaultValueStart(declaration); equals >= 0) {
        defaultValue = declaration.mid(equals + 1).trimmed();
        declaration = declaration.left(equals).trimmed();
    }
    Parameter parameter = typed ? typedParameter(declaration) : untypedParameter(declaration);
    parameter.defaultValue = std::move(defaultValue);
    parameter.text = std::move(declaration);
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
    const std::optional<TemplateHead> head = splitTemplateHead(withoutLabels(header));
    if (!head)
        return function;
    function.templateParameters = head->parameters;
    QString text = head->rest;
    // Annotations ("@Override") and attributes ("[[nodiscard]]").
    static const QRegularExpression annotation(QStringLiteral("@[A-Za-z_][\\w.]*(?:\\([^()]*\\))?"));
    for (QRegularExpressionMatchIterator it = annotation.globalMatch(text); it.hasNext();)
        function.specifiers.append(it.next().captured(0));
    text.remove(annotation);
    static const QRegularExpression attribute(QStringLiteral("\\[\\[[^\\]]*\\]\\]"));
    text.remove(attribute);
    text = text.trimmed();

    const int open = parameterListOpen(text);
    if (open <= 0)
        return function;
    const int close = closerIn(text, open);
    if (close < 0)
        return function;
    NameSplit named = splitName(text.left(open).trimmed());
    if (!isFunctionName(named.name))
        return function;
    // "Foo::bar", "Foo<T>::bar": the scope.
    const std::optional<ScopeSplit> scoped = splitScope(named.before.trimmed());
    if (!scoped)
        return function;
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    const QStringList words = scoped->before.split(spaces, Qt::SkipEmptyParts);
    function.scope = scoped->scope;
    if (isExpression(scoped->before, words))
        return function;
    const auto [specifiers, returnTypeWords] = splitSpecifiers(words);
    function.specifiers.append(specifiers);
    function.returnType = simplifiedType(returnTypeWords.join(QLatin1Char(' ')));
    function.name = std::move(named.name);
    function.parameters = text.mid(open + 1, close - open - 1).trimmed();
    function.trailing = text.mid(close + 1).simplified();
    const std::optional<QString> trailingType = trailingReturnType(function.trailing);
    const bool hasNoLeadingType = function.returnType == QLatin1String("auto") || function.returnType.isEmpty();
    if (trailingType && hasNoLeadingType)
        function.returnType = *trailingType;
    static const QRegularExpression pure(QStringLiteral("=\\s*0\\s*$"));
    static const QRegularExpression deleted(QStringLiteral("=\\s*(?:default|delete)\\s*$"));
    function.pure =
        pure.match(function.trailing).hasMatch() || function.specifiers.contains(QStringLiteral("abstract"));
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
    QString access;
    for (const DeclarationRange &range : declarationRanges(blanked, open)) {
        const QString raw = QString::fromUtf8(blanked.mid(range.from, range.to - range.from));
        access = lastAccessLabel(raw, access);
        const QString declaration = withoutLabels(raw);
        if (!declaration.isEmpty())
            result.append({declaration, range.hasBody, range.from, range.hasBody ? range.to : -1, access});
    }
    return result;
}

ClassHeader classAt(const QByteArray &blanked, int brace)
{
    ClassHeader header;
    if (brace < 0)
        return header;
    const std::optional<QString> source = classHeaderText(blanked, brace);
    if (!source)
        return header;
    static const QRegularExpression keyword(
        QStringLiteral("(?:^|[\\s;])(?:(?:public|private|protected|abstract|final|static|sealed|export|default|"
                       "declare)\\s+)*(class|struct|interface|enum|record|union)\\b(?:\\s+(?:class|struct)\\b)?"));
    const QRegularExpressionMatch match = keyword.match(*source);
    if (!match.hasMatch())
        return header;
    header.keyword = match.captured(1);
    const QString rest = source->mid(match.capturedEnd());
    // Up to the bases.
    static const QRegularExpression basesStart(QStringLiteral(":(?!:)|\\bextends\\b|\\bimplements\\b"));
    const QRegularExpressionMatch bases = basesStart.match(rest);
    const QString names = bases.hasMatch() ? rest.left(bases.capturedStart()) : rest;
    header.name = classNameIn(withoutAngleArguments(names));
    if (header.name.isEmpty() || !bases.hasMatch())
        return header;
    header.bases = baseNamesIn(rest.mid(bases.capturedStart()));
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
    constexpr qsizetype maxScannedBytes = 200000;
    int tabs = 0;
    int spaced = 0;
    int smallest = 0;
    const qsizetype end = std::min<qsizetype>(text.size(), maxScannedBytes);
    qsizetype i = 0;
    while (i < end) {
        if (text[i] == '\t') {
            ++tabs;
        } else if (const int width = spaceIndentWidth(text, i, end); width > 0) {
            ++spaced;
            smallest = smallest == 0 ? width : std::min(smallest, width);
        }
        while (i < end && text[i] != '\n')
            ++i;
        ++i;
    }
    if (tabs > spaced)
        return QStringLiteral("\t");
    return {smallest >= 2 && smallest <= 8 ? smallest : 4, QLatin1Char(' ')};
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
