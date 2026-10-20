// SPDX-License-Identifier: GPL-3.0-only

#include "Templates.h"

#include "CodeScan.h"
#include "Snippet.h"

#include <QRegularExpression>
#include <QSet>

#include <optional>
#include <vector>

namespace {

// The languages a template is for, as bit flags.
constexpr unsigned cLanguage = 1;
constexpr unsigned cppLanguage = 2;
constexpr unsigned javaLanguage = 4;
constexpr unsigned jsLanguage = 8; // and TypeScript
constexpr unsigned tsLanguage = 16;
constexpr unsigned allLanguages = cLanguage | cppLanguage | javaLanguage | jsLanguage;

// The body is a snippet: "§" stands for the expression, "¬" for its
// negation, "¶" for its text in a string literal, "¤" for a name
// suggested for its value; each tab is one indentation step of the file.
// In live templates, "{{class}}", "{{method}}", "{{parameters}}" and
// "{{variable}}" stand for the class and the method around, what prints
// the method's parameters, and the variable declared last.
struct Template {
    const char *key;
    unsigned languages;
    bool statement; // only where a statement starts
    const char *body;
    const char *import = nullptr; // the class it uses, imported in Java
};

// The postfix templates of CLion (ReSharper C++) and IntelliJ.
const std::vector<Template> &postfixTemplates()
{
    static const std::vector<Template> templates = {
        {"if", allLanguages, true, "if (§) {\n\t$0\n}"},
        {"else", allLanguages, true, "if (¬) {\n\t$0\n}"},
        {"nn", cLanguage, true, "if (§ != NULL) {\n\t$0\n}"},
        {"nn", cppLanguage, true, "if (§ != nullptr) {\n\t$0\n}"},
        {"nn", javaLanguage | jsLanguage, true, "if (§ != null) {\n\t$0\n}"},
        {"notnull", cLanguage, true, "if (§ != NULL) {\n\t$0\n}"},
        {"notnull", cppLanguage, true, "if (§ != nullptr) {\n\t$0\n}"},
        {"notnull", javaLanguage | jsLanguage, true, "if (§ != null) {\n\t$0\n}"},
        {"null", cLanguage, true, "if (§ == NULL) {\n\t$0\n}"},
        {"null", cppLanguage, true, "if (§ == nullptr) {\n\t$0\n}"},
        {"null", javaLanguage | jsLanguage, true, "if (§ == null) {\n\t$0\n}"},
        {"for", cppLanguage, true, "for (auto &${1:item} : §) {\n\t$0\n}"},
        {"for", javaLanguage, true, "for (var ${1:item} : §) {\n\t$0\n}"},
        {"for", jsLanguage, true, "for (const ${1:item} of §) {\n\t$0\n}"},
        {"fori", cLanguage | cppLanguage | javaLanguage, true, "for (int ${1:i} = 0; $1 < §; ++$1) {\n\t$0\n}"},
        {"fori", jsLanguage, true, "for (let ${1:i} = 0; $1 < §; $1++) {\n\t$0\n}"},
        {"forr", cLanguage | cppLanguage | javaLanguage, true, "for (int ${1:i} = § - 1; $1 >= 0; --$1) {\n\t$0\n}"},
        {"forr", jsLanguage, true, "for (let ${1:i} = § - 1; $1 >= 0; $1--) {\n\t$0\n}"},
        {"while", allLanguages, true, "while (§) {\n\t$0\n}"},
        {"do", allLanguages, true, "do {\n\t$0\n} while (§);"},
        {"switch", allLanguages, true, "switch (§) {\n\t$0\n}"},
        {"return", allLanguages, true, "return §;"},
        {"throw", cppLanguage | javaLanguage | jsLanguage, true, "throw §;"},
        {"var", cppLanguage, true, "auto ${1:¤} = §;$0"},
        {"var", javaLanguage, true, "var ${1:¤} = §;$0"},
        {"var", jsLanguage, true, "const ${1:¤} = §;$0"},
        {"let", jsLanguage, true, "let ${1:¤} = §;$0"},
        {"co_return", cppLanguage, true, "co_return §;"},
        {"cout", cppLanguage, true, "std::cout << § << std::endl;"},
        {"sout", javaLanguage, true, "System.out.println(§);"},
        {"serr", javaLanguage, true, "System.err.println(§);"},
        {"log", jsLanguage, true, "console.log(§);"},
        {"not", allLanguages, false, "¬"},
        {"par", allLanguages, false, "(§)"},
        {"arg", allLanguages, false, "${1:function}(§)$0"},
        {"cast", cLanguage | javaLanguage, false, "((${1:type}) §)"},
        {"as", tsLanguage, false, "(§ as ${1:Type})"},
        {"static_cast", cppLanguage, false, "static_cast<${1:type}>(§)"},
        {"dynamic_cast", cppLanguage, false, "dynamic_cast<${1:type}>(§)"},
        {"const_cast", cppLanguage, false, "const_cast<${1:type}>(§)"},
        {"reinterpret_cast", cppLanguage, false, "reinterpret_cast<${1:type}>(§)"},
        {"move", cppLanguage, false, "std::move(§)"},
        {"forward", cppLanguage, false, "std::forward<decltype(§)>(§)"},
        {"make_unique", cppLanguage, false, "std::make_unique<§>($0)"},
        {"make_shared", cppLanguage, false, "std::make_shared<§>($0)"},
        {"new", cppLanguage | javaLanguage | jsLanguage, false, "new §($0)"},
        {"begin", cppLanguage, false, "§.begin(), §.end()"},
        {"cbegin", cppLanguage, false, "§.cbegin(), §.cend()"},
        {"co_await", cppLanguage, false, "co_await §"},
        {"co_yield", cppLanguage, false, "co_yield §"},
        {"await", jsLanguage, false, "await §"},
        // IntelliJ IDEA's, for Java.
        {"iter", javaLanguage, true, "for (var ${1:item} : §) {\n\t$0\n}"},
        {"soutv", javaLanguage, true, "System.out.println(\"¶ = \" + §);"},
        {"try", javaLanguage, true, "try {\n\t§;$0\n} catch (${1:Exception} e) {\n\tthrow new RuntimeException(e);\n}"},
        {"synchronized", javaLanguage, true, "synchronized (§) {\n\t$0\n}"},
        {"assert", javaLanguage, true, "assert §;$0"},
        {"inst", javaLanguage, false, "§ instanceof ${1:Type} ${2:name}$0"},
        {"instanceof", javaLanguage, false, "§ instanceof ${1:Type} ${2:name}$0"},
        {"opt", javaLanguage, false, "Optional.ofNullable(§)", "java.util.Optional"},
        {"stream", javaLanguage, false, "Arrays.stream(§)", "java.util.Arrays"},
        {"reqnonnull", javaLanguage, false, "Objects.requireNonNull(§)", "java.util.Objects"},
        {"lambda", javaLanguage, false, "() -> §"},
        {"format", javaLanguage, false, "String.format(§, $0)"},
        // WebStorm's, for JavaScript and TypeScript.
        {"const", jsLanguage, true, "const ${1:¤} = §;$0"},
        {"forof", jsLanguage, true, "for (const ${1:item} of §) {\n\t$0\n}"},
        {"forin", jsLanguage, true, "for (const ${1:key} in §) {\n\t$0\n}"},
        {"undefined", jsLanguage, true, "if (§ === undefined) {\n\t$0\n}"},
        {"notundefined", jsLanguage, true, "if (§ !== undefined) {\n\t$0\n}"},
        {"typeof", jsLanguage, false, "typeof §"},
        {"instanceof", jsLanguage, false, "§ instanceof ${1:Type}$0"},
    };
    return templates;
}

// The live templates of CLion and IntelliJ.
const std::vector<Template> &liveTemplates()
{
    static const std::vector<Template> templates = {
        {"for", cLanguage | cppLanguage, true, "for (int ${1:i} = 0; $1 < ${2:n}; ++$1) {\n\t$0\n}"},
        {"iter", cppLanguage, true, "for (${3:auto} &${2:item} : ${1:container}) {\n\t$0\n}"},
        {"itit", cppLanguage, true, "for (auto ${2:it} = ${1:container}.begin(); $2 != $1.end(); ++$2) {\n\t$0\n}"},
        {"ritit", cppLanguage, true, "for (auto ${2:it} = ${1:container}.rbegin(); $2 != $1.rend(); ++$2) {\n\t$0\n}"},
        {"main", cLanguage | cppLanguage, true, "int main(${1:int argc, char *argv[]}) {\n\t$0\n\treturn 0;\n}"},
        {"incl", cLanguage | cppLanguage, true, "#include <${1:header}>$0"},
        {"cout", cppLanguage, true, "std::cout << ${1:value} << std::endl;$0"},
        {"lambda", cppLanguage, false, "[${1:&}](${2}) {\n\t$0\n}"},
        {"class", cppLanguage, true, "class ${1:Name} {\npublic:\n\t$0\n};"},
        {"struct", cLanguage | cppLanguage, true, "struct ${1:Name} {\n\t$0\n};"},
        {"enum", cppLanguage, true, "enum class ${1:Name} {\n\t$0\n};"},
        {"ns", cppLanguage, true, "namespace ${1:name} {\n\n$0\n\n} // namespace $1"},
        {"tryc", cppLanguage, true, "try {\n\t$0\n} catch (${1:const std::exception &e}) {\n}"},
        {"main", javaLanguage, true, "public static void main(String[] args) {\n\t$0\n}"},
        {"psvm", javaLanguage, true, "public static void main(String[] args) {\n\t$0\n}"},
        {"sout", javaLanguage, true, "System.out.println($0);"},
        {"serr", javaLanguage, true, "System.err.println($0);"},
        {"souf", javaLanguage, true, "System.out.printf(\"${1}%n\"$2);$0"},
        {"fori", javaLanguage, true, "for (int ${1:i} = 0; $1 < ${2:n}; $1++) {\n\t$0\n}"},
        {"iter", javaLanguage, true, "for (${3:var} ${2:item} : ${1:items}) {\n\t$0\n}"},
        {"ifn", javaLanguage | jsLanguage, true, "if (${1:value} == null) {\n\t$0\n}"},
        {"inn", javaLanguage | jsLanguage, true, "if (${1:value} != null) {\n\t$0\n}"},
        {"thr", javaLanguage | jsLanguage, true, "throw new $0"},
        {"psf", javaLanguage, true, "public static final $0"},
        {"prsf", javaLanguage, true, "private static final $0"},
        {"tryc", javaLanguage, true,
         "try {\n\t$0\n} catch (${1:Exception e}) {\n\t${2:throw new RuntimeException(e);}\n}"},
        // More of IntelliJ IDEA's.
        {"soutv", javaLanguage, true, "System.out.println(\"${1:{{variable}}} = \" + $1);$0"},
        {"soutm", javaLanguage, true, "System.out.println(\"{{class}}.{{method}}\");$0"},
        {"soutp", javaLanguage, true, "System.out.println({{parameters}});$0"},
        {"itar", javaLanguage, true,
         "for (int ${1:i} = 0; $1 < ${2:array}.length; $1++) {\n\t${3:var} ${4:element} = $2[$1];\n\t$0\n}"},
        {"ritar", javaLanguage, true,
         "for (int ${1:i} = ${2:array}.length - 1; $1 >= 0; $1--) {\n\t${3:var} ${4:element} = $2[$1];\n\t$0\n}"},
        {"itli", javaLanguage, true,
         "for (int ${1:i} = 0; $1 < ${2:list}.size(); $1++) {\n\t${3:var} ${4:element} = $2.get($1);\n\t$0\n}"},
        {"itco", javaLanguage, true,
         "for (Iterator<${3:Object}> ${2:iterator} = ${1:collection}.iterator(); $2.hasNext(); ) {\n\t$3 ${4:next} = "
         "$2.next();\n\t$0\n}",
         "java.util.Iterator"},
        {"inst", javaLanguage, true, "if (${1:value} instanceof ${2:Type} ${3:name}) {\n\t$0\n}"},
        {"lazy", javaLanguage, true, "if (${1:value} == null) {\n\t$1 = new ${2:Object}($3);\n}$0"},
        {"mn", javaLanguage, true, "${1:value} = Math.min($1, ${2:other});$0"},
        {"mx", javaLanguage, true, "${1:value} = Math.max($1, ${2:other});$0"},
        {"lst", javaLanguage, false, "${1:array}[$1.length - 1]$0"},
        {"toar", javaLanguage, false, "${1:list}.toArray(new ${2:Object}[0])$0"},
        {"psfs", javaLanguage, true, "public static final String "},
        {"psfi", javaLanguage, true, "public static final int "},
        {"St", javaLanguage, false, "String "},
        {"geti", javaLanguage, true, "public static {{class}} getInstance() {\n\treturn ${1:INSTANCE};\n}$0"},
        {"log", jsLanguage, true, "console.log($0);"},
        {"fori", jsLanguage, true, "for (let ${1:i} = 0; $1 < ${2:n}; $1++) {\n\t$0\n}"},
        {"iter", jsLanguage, true, "for (const ${2:item} of ${1:items}) {\n\t$0\n}"},
        {"forin", jsLanguage, true, "for (const ${2:key} in ${1:object}) {\n\t$0\n}"},
        {"arf", jsLanguage, false, "(${1}) => {\n\t$0\n}"},
        {"imp", jsLanguage, true, "import ${2:name} from '${1:module}';$0"},
        {"tryc", jsLanguage, true, "try {\n\t$0\n} catch (${1:error}) {\n}"},
        // More of WebStorm's.
        {"itar", jsLanguage, true,
         "for (let ${1:i} = 0; $1 < ${2:array}.length; $1++) {\n\tconst ${3:element} = $2[$1];\n\t$0\n}"},
        {"ritar", jsLanguage, true,
         "for (let ${1:i} = ${2:array}.length - 1; $1 >= 0; $1--) {\n\tconst ${3:element} = $2[$1];\n\t$0\n}"},
        {"forof", jsLanguage, true, "for (const ${2:item} of ${1:items}) {\n\t$0\n}"},
        {"imn", jsLanguage, true, "import * as ${2:name} from '${1:module}';$0"},
    };
    return templates;
}

unsigned languageOf(const QString &languageId)
{
    if (languageId == QLatin1String("c"))
        return cLanguage;
    if (languageId == QLatin1String("cpp"))
        return cppLanguage;
    if (languageId == QLatin1String("java"))
        return javaLanguage;
    if (languageId == QLatin1String("javascript"))
        return jsLanguage;
    if (languageId == QLatin1String("typescript") || languageId == QLatin1String("tsx"))
        return jsLanguage | tsLanguage;
    return 0;
}

using CodeScan::isIdentifierByte;
using CodeScan::startsStatement;

// How far back an expression is looked for: a call's arguments rarely span
// more.
constexpr int maxExpressionBytes = 4000;
// Expressions offered for a postfix template ("b", "a + b", "x * a + b").
constexpr int maxExpressionChoices = 6;

// The opening quote of the literal whose closing quote is at `close`.
int quoteStart(const QByteArray &text, int close, int limit)
{
    const char quote = text[close];
    for (int i = close - 1; i >= limit; --i) {
        if (text[i] == quote && (i == 0 || text[i - 1] != '\\'))
            return i;
    }
    return -1;
}

bool isQuote(char c)
{
    return c == '"' || c == '\'';
}

bool isCloser(char c)
{
    return c == ')' || c == ']' || c == '}';
}

bool isOpener(char c)
{
    return c == '(' || c == '[' || c == '{';
}

char closerOf(char opener)
{
    switch (opener) {
    case '(':
        return ')';
    case '[':
        return ']';
    default:
        return '}';
    }
}

// Whether the last pending closer is the one `opener` opens.
bool isClosedBy(const QByteArray &pending, char opener)
{
    return !pending.isEmpty() && pending.back() == closerOf(opener);
}

// The bracket opening the one closed at `close` (")" or "]").
int openerOf(const QByteArray &text, int close, int limit)
{
    QByteArray pending; // closers waiting for their opener
    for (int i = close; i >= limit; --i) {
        const char c = text[i];
        if (isQuote(c)) {
            i = quoteStart(text, i, limit);
            if (i < 0)
                return -1;
        } else if (isCloser(c)) {
            pending.append(c);
        } else if (isOpener(c)) {
            if (!isClosedBy(pending, c))
                return -1;
            pending.chop(1);
            if (pending.isEmpty())
                return i;
        }
    }
    return -1;
}

// The "<" of template arguments closed at `close`: "vector<int>".
int angleOpenerOf(const QByteArray &text, int close, int limit)
{
    int depth = 0;
    for (int i = close; i >= limit; --i) {
        const char c = text[i];
        if (c == '>') {
            ++depth;
        } else if (c == '<') {
            if (--depth == 0)
                return i;
        } else if (!isIdentifierByte(c) && !QByteArrayView(":, *&").contains(c)) {
            return -1; // not a type
        }
    }
    return -1;
}

// Postfix or live: how a template is labeled and ranked.
struct TemplateKind {
    QString label;
    int priority = 0;
};

// The candidate of a template: matched on its key, described by what it
// gives (with "expr" for the expression), showing the real result beside
// the list.
CompletionItem itemOf(const Template &entry, const QString &snippet, const QString &sample, const TemplateKind &kind)
{
    CompletionItem item;
    item.label = QString::fromLatin1(entry.key);
    item.insertText = snippet;
    item.isSnippet = true;
    item.kind = CompletionItem::Kind::Snippet;
    item.priority = kind.priority;
    QString detail = Snippet::expand(sample).text.simplified();
    if (detail.size() > 48)
        detail = detail.left(47) + QChar(0x2026);
    item.detail = std::move(detail);
    item.documentation = kind.label + QStringLiteral("\n\n") + Snippet::expand(snippet).text;
    return item;
}

// The import of `qualified` ("java.util.Optional") in a Java file, unless
// it has it already (or the class is of java.lang or of its package).
QList<TextEdit> javaImport(const QByteArray &text, const QString &qualified)
{
    const qsizetype dot = qualified.lastIndexOf(QLatin1Char('.'));
    const QString package = qualified.left(dot);
    if (package == QLatin1String("java.lang"))
        return {};
    const QString source = QString::fromUtf8(text);
    static const QRegularExpression packageLine(QStringLiteral("^\\s*package\\s+([\\w.]+)\\s*;"),
                                                QRegularExpression::MultilineOption);
    const QRegularExpressionMatch own = packageLine.match(source);
    if (own.hasMatch() && own.captured(1) == package)
        return {};
    const QRegularExpression imported(
        QStringLiteral("^\\s*import\\s+(?:%1|%2\\.\\*)\\s*;")
            .arg(QRegularExpression::escape(qualified), QRegularExpression::escape(package)),
        QRegularExpression::MultilineOption);
    if (imported.match(source).hasMatch())
        return {};
    // After the last import, else after the package, else at the top.
    static const QRegularExpression importLine(QStringLiteral("^\\s*import\\s+[\\w.*\\s]+;[^\\n]*$"),
                                               QRegularExpression::MultilineOption);
    qsizetype after = -1;
    for (QRegularExpressionMatchIterator it = importLine.globalMatch(source); it.hasNext();)
        after = it.next().capturedEnd();
    const QString line = QStringLiteral("import %1;\n").arg(qualified);
    if (after < 0 && own.hasMatch()) {
        const int lineIndex = int(source.left(own.capturedEnd()).count(QLatin1Char('\n'))) + 1;
        return {{lineIndex, 0, lineIndex, 0, QStringLiteral("\n") + line}};
    }
    if (after < 0)
        return {{0, 0, 0, 0, line + QStringLiteral("\n")}};
    const int lineIndex = int(source.left(after).count(QLatin1Char('\n'))) + 1;
    return {{lineIndex, 0, lineIndex, 0, line}};
}

// The expression as the text of a string literal.
QString quoted(const QString &expression)
{
    QString text = expression;
    text.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\""));
    return text.simplified();
}

} // namespace

// PostfixTemplateProvider

namespace {

// "new Foo()": the object created, not the constructor's result. The start
// of the "new" before text[start], if any, else `start`.
int includingNew(const QByteArray &text, int start)
{
    int before = start;
    while (before > 0 && text[before - 1] == ' ')
        --before;
    const bool followsNew = before >= 3 && before < start && text.mid(before - 3, 3) == "new";
    if (!followsNew)
        return start;
    const bool isWholeWord = before == 3 || !isIdentifierByte(text[before - 4]);
    return isWholeWord ? before - 3 : start;
}

// Reads backwards the operand that ends at `end`: a chain of names, calls,
// subscripts and member accesses, or a parenthesized expression.
class OperandReader {
public:
    OperandReader(const QByteArray &text, int end, int limit)
        : text_(text)
        , end_(end)
        , limit_(limit)
        , pos_(end)
    {
    }

    // The start of the operand, -1 if there is none.
    int start()
    {
        while (pos_ > limit_) {
            const char c = text_[pos_ - 1];
            if (state_ == After::Connector) {
                if (!readOperand(c))
                    break;
                continue;
            }
            if (state_ == After::Group && readBeforeGroup(c))
                continue;
            if (!readConnector(c))
                break;
        }
        if (state_ == After::Connector && scopeConnector_)
            start_ = pos_; // "::global"
        if (start_ < 0 || start_ >= end_)
            return -1;
        return includingNew(text_, start_);
    }

private:
    enum class After {
        Connector,
        Name,
        Group
    };

    const QByteArray &text_;
    const int end_;
    const int limit_;
    int pos_;
    int start_ = -1;
    After state_ = After::Connector;
    bool scopeConnector_ = false;

    void skipName()
    {
        while (pos_ > limit_ && isIdentifierByte(text_[pos_ - 1]))
            --pos_;
    }

    // An operand is needed; false if `c` ends none.
    bool readOperand(char c)
    {
        if (c == ')' || c == ']') {
            const int opener = openerOf(text_, pos_ - 1, limit_);
            if (opener < 0)
                return false;
            pos_ = opener;
            state_ = After::Group;
        } else if (c == '"' || c == '\'' || c == '`') {
            const int opener = quoteStart(text_, pos_ - 1, limit_);
            if (opener < 0)
                return false;
            pos_ = opener;
            state_ = After::Name;
        } else if (isIdentifierByte(c)) {
            skipName();
            state_ = After::Name;
        } else {
            return false;
        }
        start_ = pos_;
        return true;
    }

    // The callee of a call, or the array of a subscript; false if `c` is
    // neither (a connector may still come).
    bool readBeforeGroup(char c)
    {
        if (isIdentifierByte(c)) {
            skipName();
            start_ = pos_;
            state_ = After::Name;
            return true;
        }
        if (c == ')' || c == ']') {
            state_ = After::Connector;
            return true; // the next group, read as an operand
        }
        if (c == '>')
            return readTemplateCallee();
        return false;
    }

    // "make<int>(x)".
    bool readTemplateCallee()
    {
        const int opener = angleOpenerOf(text_, pos_ - 1, limit_);
        const bool isCallee = opener > limit_ && isIdentifierByte(text_[opener - 1]);
        if (!isCallee)
            return false;
        pos_ = opener;
        skipName();
        start_ = pos_;
        state_ = After::Name;
        return true;
    }

    // A member access or a scope before the operand; false if none.
    bool readConnector(char c)
    {
        const char previous = pos_ >= 2 ? text_[pos_ - 2] : '\0';
        if (c == '.' && previous != '.') {
            pos_ -= previous == '?' ? 2 : 1;
            scopeConnector_ = false;
        } else if (c == '>' && previous == '-') {
            pos_ -= 2;
            scopeConnector_ = false;
        } else if (c == ':' && previous == ':') {
            pos_ -= 2;
            scopeConnector_ = true;
        } else {
            return false;
        }
        state_ = After::Connector;
        return true;
    }
};

int operandStart(const QByteArray &text, int end, int limit)
{
    return OperandReader(text, end, limit).start();
}

int skipBlanksBack(const QByteArray &text, int i, int limit)
{
    while (i > limit && (text[i - 1] == ' ' || text[i - 1] == '\t' || text[i - 1] == '\n' || text[i - 1] == '\r'))
        --i;
    return i;
}

// "!ready.if": a negation (not a "!=") ends at text[i].
bool isNegationBefore(const QByteArray &text, int i, int limit)
{
    const bool isInequality = i < text.size() && text[i] == '=';
    return i > limit && text[i - 1] == '!' && !isInequality;
}

// The length of the binary operator that ends at text[i]: "a + b.if";
// 0 if there is none, or an assignment, an increment.
int binaryOperatorLengthBefore(const QByteArray &text, int i, int limit)
{
    if (i - 2 >= limit) {
        static const QSet<QByteArray> binary = {"&&", "||", "==", "!=", "<=", ">=", "<<", ">>"};
        static const QSet<QByteArray> notBinary = {"++", "--", "->", "::"};
        const QByteArray two = text.mid(i - 2, 2);
        if (binary.contains(two))
            return 2;
        const bool isAssignment = two[1] == '=' && two[0] != ' ';
        if (notBinary.contains(two) || isAssignment)
            return 0;
    }
    const bool isSingle = i - 1 >= limit && QByteArrayView("+-*/%<>&|^").contains(text[i - 1]);
    return isSingle ? 1 : 0;
}

// Arguments and subscripts at the end of `text` go.
QString withoutTrailingGroups(QString text)
{
    while (text.endsWith(QLatin1Char(')')) || text.endsWith(QLatin1Char(']'))) {
        const QByteArray bytes = text.toUtf8();
        const int opener = openerOf(bytes, int(bytes.size()) - 1, 0);
        if (opener < 0)
            break;
        text = QString::fromUtf8(bytes.left(opener)).trimmed();
    }
    return text;
}

// "getValue" → "Value", "is_open" → "open".
QString withoutAccessorPrefix(const QString &name)
{
    for (const QLatin1String prefix : {QLatin1String("get"), QLatin1String("is"), QLatin1String("to")}) {
        if (name.size() <= prefix.size() || !name.startsWith(prefix))
            continue;
        const QChar next = name[prefix.size()];
        if (next.isUpper())
            return name.mid(prefix.size());
        if (next == QLatin1Char('_') && name.size() > prefix.size() + 1)
            return name.mid(prefix.size() + 1);
    }
    return name;
}

} // namespace

int PostfixTemplateProvider::expressionStart(const QByteArray &text, int dot)
{
    if (dot <= 0 || dot >= text.size() || text[dot] != '.')
        return -1;
    const int limit = std::max(0, dot - maxExpressionBytes);
    int end = dot;
    if (end > 0 && text[end - 1] == '?') // "a?.b"
        --end;
    return operandStart(text, end, limit);
}

QList<int> PostfixTemplateProvider::expressionStarts(const QByteArray &text, int dot)
{
    QList<int> starts;
    int start = expressionStart(text, dot);
    if (start < 0)
        return starts;
    starts.append(start);
    const int limit = std::max(0, dot - maxExpressionBytes);
    while (starts.size() < maxExpressionChoices) {
        const int i = skipBlanksBack(text, start, limit);
        if (isNegationBefore(text, i, limit)) {
            start = i - 1;
            starts.append(start);
            continue;
        }
        // A binary operator, then its left operand.
        const int length = binaryOperatorLengthBefore(text, i, limit);
        if (length == 0)
            break;
        const int operandEnd = skipBlanksBack(text, i - length, limit);
        if (operandEnd <= limit)
            break;
        const int operand = operandStart(text, operandEnd, limit);
        if (operand < 0)
            break;
        start = operand;
        starts.append(start);
    }
    return starts;
}

QString PostfixTemplateProvider::suggestName(const QString &expression)
{
    // The name the value comes from: the last one, outside arguments.
    static const QRegularExpression trailing(QStringLiteral("([A-Za-z_][A-Za-z0-9_]*)\\s*(?:<[^<>]*>)?\\s*$"));
    const QString text = withoutTrailingGroups(expression.trimmed());
    QString name = withoutAccessorPrefix(trailing.match(text).captured(1));
    static const QStringList reserved = {"new", "this", "std", "make_unique", "make_shared", "self"};
    if (name.isEmpty() || reserved.contains(name) || name == expression.trimmed())
        return QStringLiteral("value");
    name[0] = name[0].toLower();
    return name;
}

bool PostfixTemplateProvider::handles(const CompletionContext &context) const
{
    return languageOf(context.languageId) != 0 && context.prefixStart > 0 &&
           context.prefixStart <= context.text.size() && context.text[context.prefixStart - 1] == '.';
}

QString PostfixTemplateProvider::triggerCharacters(const QString &languageId) const
{
    return languageOf(languageId) != 0 ? QStringLiteral(".") : QString();
}

namespace {

// An expression a postfix template can wrap.
struct PostfixCandidate {
    int start = 0;
    QString expression;
    bool compound = false; // "a + b": parenthesized where it is an operand
    bool statement = false;
    QString indent;
    QString name;
};

// Each expression before the dot that a template can wrap: the nearest first.
QList<PostfixCandidate> postfixCandidates(const QByteArray &text, const QList<int> &starts, int end)
{
    QList<PostfixCandidate> candidates;
    for (int i = 0; i < starts.size(); ++i) {
        const QString expression = QString::fromUtf8(text.mid(starts[i], end - starts[i]));
        candidates.append({starts[i], expression, i > 0 && !expression.startsWith(QLatin1Char('!')),
                           startsStatement(text, starts[i]), CodeScan::lineIndent(text, starts[i]),
                           PostfixTemplateProvider::suggestName(expression)});
    }
    return candidates;
}

bool startsWithDigit(const QByteArray &text, int start)
{
    return text[start] >= '0' && text[start] <= '9';
}

// "((type) a + b)" and "a + b.begin()" need the parentheses.
bool wrapsAnOperand(const Template &entry)
{
    static const QSet<QString> operands = {"cast", "begin", "cbegin", "co_await", "co_yield", "await", "new"};
    return operands.contains(QString::fromLatin1(entry.key));
}

// The body of `entry` around `value`, at the indentation of `candidate`.
QString filledSnippet(const Template &entry,
                      const PostfixCandidate &candidate,
                      const QString &value,
                      const QString &unit)
{
    QString snippet = CodeScan::indented(QString::fromUtf8(entry.body), candidate.indent, unit);
    const QString wrapped = candidate.compound ? QLatin1Char('(') + value + QLatin1Char(')') : value;
    snippet.replace(QChar(0x00AC), Snippet::escape(QLatin1Char('!') + wrapped));              // ¬
    snippet.replace(QChar(0x00B6), Snippet::escape(quoted(value)));                           // ¶
    snippet.replace(QChar(0x00A7), Snippet::escape(wrapsAnOperand(entry) ? wrapped : value)); // §
    snippet.replace(QChar(0x00A4), candidate.name);                                           // ¤
    return snippet;
}

QList<const PostfixCandidate *> fittingCandidates(const Template &entry, const QList<PostfixCandidate> &candidates)
{
    QList<const PostfixCandidate *> fitting;
    for (const PostfixCandidate &candidate : candidates) {
        if (!entry.statement || candidate.statement)
            fitting.append(&candidate);
    }
    return fitting;
}

QString choiceLabel(const QString &expression)
{
    QString label = expression.simplified();
    if (label.size() > 60)
        label = label.left(59) + QChar(0x2026);
    return label;
}

// The item of `entry` on the nearest fitting expression, the others as
// choices.
CompletionItem postfixItem(const Template &entry,
                           const QList<const PostfixCandidate *> &fitting,
                           const QString &unit,
                           const TemplateKind &kind)
{
    const PostfixCandidate &nearest = *fitting.first();
    CompletionItem item = itemOf(entry, filledSnippet(entry, nearest, nearest.expression, unit),
                                 filledSnippet(entry, nearest, QStringLiteral("expr"), unit), kind);
    item.replaceStart = nearest.start;
    if (fitting.size() > 1) {
        for (const PostfixCandidate *candidate : fitting)
            item.choices.append({choiceLabel(candidate->expression),
                                 filledSnippet(entry, *candidate, candidate->expression, unit), candidate->start});
    }
    return item;
}

} // namespace

void PostfixTemplateProvider::complete(const CompletionContext &context, const Callback &done)
{
    const int dot = context.prefixStart - 1;
    const QList<int> starts = expressionStarts(context.text, dot);
    // Nothing before the dot, or a number ("3.f").
    if (starts.isEmpty() || startsWithDigit(context.text, starts.first())) {
        done({}, false);
        return;
    }
    const unsigned language = languageOf(context.languageId);
    int end = dot;
    if (context.text[end - 1] == '?')
        --end;
    const QString unit = CodeScan::indentUnit(context.text);
    const TemplateKind postfix{QObject::tr("Postfix template"), 3};
    const QList<PostfixCandidate> candidates = postfixCandidates(context.text, starts, end);

    QList<CompletionItem> items;
    for (const Template &entry : postfixTemplates()) {
        if (!(entry.languages & language))
            continue;
        const QList<const PostfixCandidate *> fitting = fittingCandidates(entry, candidates);
        if (fitting.isEmpty())
            continue;
        CompletionItem item = postfixItem(entry, fitting, unit, postfix);
        if (entry.import)
            item.additionalEdits = javaImport(context.text, QString::fromLatin1(entry.import));
        items.append(item);
    }
    done(items, false);
}

// LiveTemplateProvider

namespace {

// What a template says of where it is written.
struct Surroundings {
    QString className;         // of the class around
    CodeScan::Function method; // the function around, an empty name if none
    QString variable;          // the variable declared last before, "value" if none
};

// The function whose body the brace at `brace` opens; nullopt for the
// block of a statement ("if (x) {").
std::optional<CodeScan::Function> functionOpenedAt(const QByteArray &blanked, int brace)
{
    const int start = CodeScan::headerStart(blanked, brace);
    CodeScan::Function function =
        CodeScan::parseFunction(QString::fromUtf8(blanked.mid(start, brace - start)).trimmed());
    static const QSet<QString> statements = {"if", "for", "while", "switch", "catch", "synchronized", "try"};
    if (function.name.isEmpty() || statements.contains(function.name))
        return std::nullopt;
    return function;
}

// "Type name =", "Type name;", "for (Type name :": the last one in `body`.
QString lastDeclaredVariable(const QString &body)
{
    static const QRegularExpression declaration(
        QStringLiteral("\\b([A-Za-z_][\\w.]*(?:<[^;{}()=]*>)?(?:\\[\\])*)\\s+([A-Za-z_]\\w*)\\s*(?:=(?!=)|;|:(?!:))"));
    static const QSet<QString> notTypes = {"return", "new", "throw", "else", "case", "package", "import", "yield"};
    QString variable;
    for (QRegularExpressionMatchIterator it = declaration.globalMatch(body); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        if (!notTypes.contains(match.captured(1)))
            variable = match.captured(2);
    }
    return variable;
}

Surroundings surroundings(const QByteArray &text, int position)
{
    Surroundings result;
    const QByteArray blanked = CodeScan::blank(text.left(position));
    int bodyStart = std::max(0, position - maxExpressionBytes);
    for (int brace = CodeScan::enclosingBrace(blanked, position); brace >= 0;
         brace = CodeScan::enclosingBrace(blanked, brace)) {
        const CodeScan::ClassHeader header = CodeScan::classAt(blanked, brace);
        if (!header.name.isEmpty()) {
            result.className = header.name;
            break;
        }
        if (!result.method.name.isEmpty())
            continue;
        if (const std::optional<CodeScan::Function> function = functionOpenedAt(blanked, brace)) {
            result.method = *function;
            bodyStart = std::max(bodyStart, brace + 1);
        }
    }
    result.variable = lastDeclaredVariable(QString::fromUtf8(blanked.mid(bodyStart, position - bodyStart)));
    if (result.variable.isEmpty()) {
        const QList<CodeScan::Parameter> parameters = result.method.parameterList();
        if (!parameters.isEmpty())
            result.variable = parameters.last().name;
    }
    if (result.variable.isEmpty())
        result.variable = QStringLiteral("value");
    return result;
}

// What prints the parameters of `function`: "\"a = \" + a + \", b = \" + b".
QString printedParameters(const CodeScan::Function &function)
{
    QStringList parts;
    for (const CodeScan::Parameter &parameter : function.parameterList()) {
        if (parameter.name.isEmpty())
            continue;
        parts.append(
            QStringLiteral("\"%1%2 = \" + %2").arg(parts.isEmpty() ? QString() : QStringLiteral(", "), parameter.name));
    }
    return parts.isEmpty() ? QStringLiteral("\"\"") : parts.join(QStringLiteral(" + "));
}

// Whether `entry` is offered in a file of `language`, where a statement
// starts or not.
bool fitsLiveTemplate(const Template &entry, unsigned language, bool atStatementStart)
{
    const bool isOtherLanguage = !(entry.languages & language);
    const bool needsStatement = entry.statement && !atStatementStart;
    return !isOtherLanguage && !needsStatement;
}

// `snippet` with its "{{...}}" filled from `around`; nullopt if it needs a
// class or a method that is not there.
std::optional<QString> withSurroundings(QString snippet, const Surroundings &around)
{
    if (snippet.contains(QLatin1String("{{class}}")) && around.className.isEmpty())
        return std::nullopt;
    if (snippet.contains(QLatin1String("{{method}}")) && around.method.name.isEmpty())
        return std::nullopt;
    snippet.replace(QLatin1String("{{class}}"), Snippet::escape(around.className))
        .replace(QLatin1String("{{method}}"), Snippet::escape(around.method.name))
        .replace(QLatin1String("{{parameters}}"), Snippet::escape(printedParameters(around.method)))
        .replace(QLatin1String("{{variable}}"), Snippet::escape(around.variable));
    return snippet;
}

} // namespace

bool LiveTemplateProvider::handles(const CompletionContext &context) const
{
    return languageOf(context.languageId) != 0;
}

void LiveTemplateProvider::complete(const CompletionContext &context, const Callback &done)
{
    QList<CompletionItem> items;
    if (!context.triggerCharacter.isNull()) {
        done(items, false); // a member is expected
        return;
    }
    const unsigned language = languageOf(context.languageId);
    const bool statement = startsStatement(context.text, context.prefixStart);
    const QString indent = CodeScan::lineIndent(context.text, context.prefixStart);
    const QString unit = CodeScan::indentUnit(context.text);
    const TemplateKind live{QObject::tr("Live template"), 1}; // below the keyword of the same name ("for")
    std::optional<Surroundings> around;                       // read for the first template that needs it
    for (const Template &entry : liveTemplates()) {
        if (!fitsLiveTemplate(entry, language, statement))
            continue;
        QString snippet = CodeScan::indented(QString::fromUtf8(entry.body), indent, unit);
        if (snippet.contains(QLatin1String("{{"))) {
            if (!around)
                around = surroundings(context.text, context.prefixStart);
            std::optional<QString> filled = withSurroundings(snippet, *around);
            if (!filled)
                continue;
            snippet = std::move(*filled);
        }
        CompletionItem item = itemOf(entry, snippet, snippet, live);
        if (entry.import)
            item.additionalEdits = javaImport(context.text, QString::fromLatin1(entry.import));
        items.append(item);
    }
    done(items, false);
}
