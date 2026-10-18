#include "Templates.h"

#include "CodeScan.h"
#include "Snippet.h"

#include <QRegularExpression>
#include <QSet>

#include <optional>
#include <vector>

namespace {

enum Language : unsigned {
    C = 1,
    Cpp = 2,
    Java = 4,
    Js = 8, // and TypeScript
    Ts = 16,
    All = C | Cpp | Java | Js,
};

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
        {"if", All, true, "if (§) {\n\t$0\n}"},
        {"else", All, true, "if (¬) {\n\t$0\n}"},
        {"nn", C, true, "if (§ != NULL) {\n\t$0\n}"},
        {"nn", Cpp, true, "if (§ != nullptr) {\n\t$0\n}"},
        {"nn", Java | Js, true, "if (§ != null) {\n\t$0\n}"},
        {"notnull", C, true, "if (§ != NULL) {\n\t$0\n}"},
        {"notnull", Cpp, true, "if (§ != nullptr) {\n\t$0\n}"},
        {"notnull", Java | Js, true, "if (§ != null) {\n\t$0\n}"},
        {"null", C, true, "if (§ == NULL) {\n\t$0\n}"},
        {"null", Cpp, true, "if (§ == nullptr) {\n\t$0\n}"},
        {"null", Java | Js, true, "if (§ == null) {\n\t$0\n}"},
        {"for", Cpp, true, "for (auto &${1:item} : §) {\n\t$0\n}"},
        {"for", Java, true, "for (var ${1:item} : §) {\n\t$0\n}"},
        {"for", Js, true, "for (const ${1:item} of §) {\n\t$0\n}"},
        {"fori", C | Cpp | Java, true, "for (int ${1:i} = 0; $1 < §; ++$1) {\n\t$0\n}"},
        {"fori", Js, true, "for (let ${1:i} = 0; $1 < §; $1++) {\n\t$0\n}"},
        {"forr", C | Cpp | Java, true, "for (int ${1:i} = § - 1; $1 >= 0; --$1) {\n\t$0\n}"},
        {"forr", Js, true, "for (let ${1:i} = § - 1; $1 >= 0; $1--) {\n\t$0\n}"},
        {"while", All, true, "while (§) {\n\t$0\n}"},
        {"do", All, true, "do {\n\t$0\n} while (§);"},
        {"switch", All, true, "switch (§) {\n\t$0\n}"},
        {"return", All, true, "return §;"},
        {"throw", Cpp | Java | Js, true, "throw §;"},
        {"var", Cpp, true, "auto ${1:¤} = §;$0"},
        {"var", Java, true, "var ${1:¤} = §;$0"},
        {"var", Js, true, "const ${1:¤} = §;$0"},
        {"let", Js, true, "let ${1:¤} = §;$0"},
        {"co_return", Cpp, true, "co_return §;"},
        {"cout", Cpp, true, "std::cout << § << std::endl;"},
        {"sout", Java, true, "System.out.println(§);"},
        {"serr", Java, true, "System.err.println(§);"},
        {"log", Js, true, "console.log(§);"},
        {"not", All, false, "¬"},
        {"par", All, false, "(§)"},
        {"arg", All, false, "${1:function}(§)$0"},
        {"cast", C | Java, false, "((${1:type}) §)"},
        {"as", Ts, false, "(§ as ${1:Type})"},
        {"static_cast", Cpp, false, "static_cast<${1:type}>(§)"},
        {"dynamic_cast", Cpp, false, "dynamic_cast<${1:type}>(§)"},
        {"const_cast", Cpp, false, "const_cast<${1:type}>(§)"},
        {"reinterpret_cast", Cpp, false, "reinterpret_cast<${1:type}>(§)"},
        {"move", Cpp, false, "std::move(§)"},
        {"forward", Cpp, false, "std::forward<decltype(§)>(§)"},
        {"make_unique", Cpp, false, "std::make_unique<§>($0)"},
        {"make_shared", Cpp, false, "std::make_shared<§>($0)"},
        {"new", Cpp | Java | Js, false, "new §($0)"},
        {"begin", Cpp, false, "§.begin(), §.end()"},
        {"cbegin", Cpp, false, "§.cbegin(), §.cend()"},
        {"co_await", Cpp, false, "co_await §"},
        {"co_yield", Cpp, false, "co_yield §"},
        {"await", Js, false, "await §"},
        // IntelliJ IDEA's, for Java.
        {"iter", Java, true, "for (var ${1:item} : §) {\n\t$0\n}"},
        {"soutv", Java, true, "System.out.println(\"¶ = \" + §);"},
        {"try", Java, true, "try {\n\t§;$0\n} catch (${1:Exception} e) {\n\tthrow new RuntimeException(e);\n}"},
        {"synchronized", Java, true, "synchronized (§) {\n\t$0\n}"},
        {"assert", Java, true, "assert §;$0"},
        {"inst", Java, false, "§ instanceof ${1:Type} ${2:name}$0"},
        {"instanceof", Java, false, "§ instanceof ${1:Type} ${2:name}$0"},
        {"opt", Java, false, "Optional.ofNullable(§)", "java.util.Optional"},
        {"stream", Java, false, "Arrays.stream(§)", "java.util.Arrays"},
        {"reqnonnull", Java, false, "Objects.requireNonNull(§)", "java.util.Objects"},
        {"lambda", Java, false, "() -> §"},
        {"format", Java, false, "String.format(§, $0)"},
        // WebStorm's, for JavaScript and TypeScript.
        {"const", Js, true, "const ${1:¤} = §;$0"},
        {"forof", Js, true, "for (const ${1:item} of §) {\n\t$0\n}"},
        {"forin", Js, true, "for (const ${1:key} in §) {\n\t$0\n}"},
        {"undefined", Js, true, "if (§ === undefined) {\n\t$0\n}"},
        {"notundefined", Js, true, "if (§ !== undefined) {\n\t$0\n}"},
        {"typeof", Js, false, "typeof §"},
        {"instanceof", Js, false, "§ instanceof ${1:Type}$0"},
    };
    return templates;
}

// The live templates of CLion and IntelliJ.
const std::vector<Template> &liveTemplates()
{
    static const std::vector<Template> templates = {
        {"for", C | Cpp, true, "for (int ${1:i} = 0; $1 < ${2:n}; ++$1) {\n\t$0\n}"},
        {"iter", Cpp, true, "for (${3:auto} &${2:item} : ${1:container}) {\n\t$0\n}"},
        {"itit", Cpp, true, "for (auto ${2:it} = ${1:container}.begin(); $2 != $1.end(); ++$2) {\n\t$0\n}"},
        {"ritit", Cpp, true, "for (auto ${2:it} = ${1:container}.rbegin(); $2 != $1.rend(); ++$2) {\n\t$0\n}"},
        {"main", C | Cpp, true, "int main(${1:int argc, char *argv[]}) {\n\t$0\n\treturn 0;\n}"},
        {"incl", C | Cpp, true, "#include <${1:header}>$0"},
        {"cout", Cpp, true, "std::cout << ${1:value} << std::endl;$0"},
        {"lambda", Cpp, false, "[${1:&}](${2}) {\n\t$0\n}"},
        {"class", Cpp, true, "class ${1:Name} {\npublic:\n\t$0\n};"},
        {"struct", C | Cpp, true, "struct ${1:Name} {\n\t$0\n};"},
        {"enum", Cpp, true, "enum class ${1:Name} {\n\t$0\n};"},
        {"ns", Cpp, true, "namespace ${1:name} {\n\n$0\n\n} // namespace $1"},
        {"tryc", Cpp, true, "try {\n\t$0\n} catch (${1:const std::exception &e}) {\n}"},
        {"main", Java, true, "public static void main(String[] args) {\n\t$0\n}"},
        {"psvm", Java, true, "public static void main(String[] args) {\n\t$0\n}"},
        {"sout", Java, true, "System.out.println($0);"},
        {"serr", Java, true, "System.err.println($0);"},
        {"souf", Java, true, "System.out.printf(\"${1}%n\"$2);$0"},
        {"fori", Java, true, "for (int ${1:i} = 0; $1 < ${2:n}; $1++) {\n\t$0\n}"},
        {"iter", Java, true, "for (${3:var} ${2:item} : ${1:items}) {\n\t$0\n}"},
        {"ifn", Java | Js, true, "if (${1:value} == null) {\n\t$0\n}"},
        {"inn", Java | Js, true, "if (${1:value} != null) {\n\t$0\n}"},
        {"thr", Java | Js, true, "throw new $0"},
        {"psf", Java, true, "public static final $0"},
        {"prsf", Java, true, "private static final $0"},
        {"tryc", Java, true, "try {\n\t$0\n} catch (${1:Exception e}) {\n\t${2:throw new RuntimeException(e);}\n}"},
        // More of IntelliJ IDEA's.
        {"soutv", Java, true, "System.out.println(\"${1:{{variable}}} = \" + $1);$0"},
        {"soutm", Java, true, "System.out.println(\"{{class}}.{{method}}\");$0"},
        {"soutp", Java, true, "System.out.println({{parameters}});$0"},
        {"itar", Java, true,
         "for (int ${1:i} = 0; $1 < ${2:array}.length; $1++) {\n\t${3:var} ${4:element} = $2[$1];\n\t$0\n}"},
        {"ritar", Java, true,
         "for (int ${1:i} = ${2:array}.length - 1; $1 >= 0; $1--) {\n\t${3:var} ${4:element} = $2[$1];\n\t$0\n}"},
        {"itli", Java, true,
         "for (int ${1:i} = 0; $1 < ${2:list}.size(); $1++) {\n\t${3:var} ${4:element} = $2.get($1);\n\t$0\n}"},
        {"itco", Java, true,
         "for (Iterator<${3:Object}> ${2:iterator} = ${1:collection}.iterator(); $2.hasNext(); ) {\n\t$3 ${4:next} = "
         "$2.next();\n\t$0\n}",
         "java.util.Iterator"},
        {"inst", Java, true, "if (${1:value} instanceof ${2:Type} ${3:name}) {\n\t$0\n}"},
        {"lazy", Java, true, "if (${1:value} == null) {\n\t$1 = new ${2:Object}($3);\n}$0"},
        {"mn", Java, true, "${1:value} = Math.min($1, ${2:other});$0"},
        {"mx", Java, true, "${1:value} = Math.max($1, ${2:other});$0"},
        {"lst", Java, false, "${1:array}[$1.length - 1]$0"},
        {"toar", Java, false, "${1:list}.toArray(new ${2:Object}[0])$0"},
        {"psfs", Java, true, "public static final String "},
        {"psfi", Java, true, "public static final int "},
        {"St", Java, false, "String "},
        {"geti", Java, true, "public static {{class}} getInstance() {\n\treturn ${1:INSTANCE};\n}$0"},
        {"log", Js, true, "console.log($0);"},
        {"fori", Js, true, "for (let ${1:i} = 0; $1 < ${2:n}; $1++) {\n\t$0\n}"},
        {"iter", Js, true, "for (const ${2:item} of ${1:items}) {\n\t$0\n}"},
        {"forin", Js, true, "for (const ${2:key} in ${1:object}) {\n\t$0\n}"},
        {"arf", Js, false, "(${1}) => {\n\t$0\n}"},
        {"imp", Js, true, "import ${2:name} from '${1:module}';$0"},
        {"tryc", Js, true, "try {\n\t$0\n} catch (${1:error}) {\n}"},
        // More of WebStorm's.
        {"itar", Js, true,
         "for (let ${1:i} = 0; $1 < ${2:array}.length; $1++) {\n\tconst ${3:element} = $2[$1];\n\t$0\n}"},
        {"ritar", Js, true,
         "for (let ${1:i} = ${2:array}.length - 1; $1 >= 0; $1--) {\n\tconst ${3:element} = $2[$1];\n\t$0\n}"},
        {"forof", Js, true, "for (const ${2:item} of ${1:items}) {\n\t$0\n}"},
        {"imn", Js, true, "import * as ${2:name} from '${1:module}';$0"},
    };
    return templates;
}

unsigned languageOf(const QString &languageId)
{
    if (languageId == QLatin1String("c"))
        return C;
    if (languageId == QLatin1String("cpp"))
        return Cpp;
    if (languageId == QLatin1String("java"))
        return Java;
    if (languageId == QLatin1String("javascript"))
        return Js;
    if (languageId == QLatin1String("typescript") || languageId == QLatin1String("tsx"))
        return Js | Ts;
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

// The bracket opening the one closed at `close` (")" or "]").
int openerOf(const QByteArray &text, int close, int limit)
{
    QByteArray pending; // closers waiting for their opener
    for (int i = close; i >= limit; --i) {
        const char c = text[i];
        if (c == '"' || c == '\'') {
            i = quoteStart(text, i, limit);
            if (i < 0)
                return -1;
        } else if (c == ')' || c == ']' || c == '}') {
            pending.append(c);
        } else if (c == '(' || c == '[' || c == '{') {
            const char closer = c == '(' ? ')' : c == '[' ? ']' : '}';
            if (pending.isEmpty() || pending.back() != closer)
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

// The candidate of a template: matched on its key, described by what it
// gives (with "expr" for the expression), showing the real result beside
// the list.
CompletionItem itemOf(const Template &entry,
                      const QString &snippet,
                      const QString &sample,
                      const QString &kindLabel,
                      int priority)
{
    CompletionItem item;
    item.label = QString::fromLatin1(entry.key);
    item.insertText = snippet;
    item.isSnippet = true;
    item.kind = CompletionItem::Kind::Snippet;
    item.priority = priority;
    QString detail = Snippet::expand(sample).text.simplified();
    if (detail.size() > 48)
        detail = detail.left(47) + QChar(0x2026);
    item.detail = std::move(detail);
    item.documentation = kindLabel + QStringLiteral("\n\n") + Snippet::expand(snippet).text;
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

// Start of the operand that ends at `end`: a chain of names, calls,
// subscripts and member accesses, or a parenthesized expression.
int operandStart(const QByteArray &text, int end, int limit)
{
    enum class After {
        Connector,
        Name,
        Group
    };
    After state = After::Connector;
    bool scopeConnector = false;
    int start = -1;
    int pos = end;
    while (pos > limit) {
        const char c = text[pos - 1];
        if (state == After::Connector) {
            // An operand is needed.
            if (c == ')' || c == ']') {
                const int opener = openerOf(text, pos - 1, limit);
                if (opener < 0)
                    break;
                pos = opener;
                state = After::Group;
            } else if (c == '"' || c == '\'' || c == '`') {
                const int opener = quoteStart(text, pos - 1, limit);
                if (opener < 0)
                    break;
                pos = opener;
                state = After::Name;
            } else if (isIdentifierByte(c)) {
                while (pos > limit && isIdentifierByte(text[pos - 1]))
                    --pos;
                state = After::Name;
            } else {
                break;
            }
            start = pos;
            continue;
        }
        if (state == After::Group) {
            // The callee of a call, or the array of a subscript.
            if (isIdentifierByte(c)) {
                while (pos > limit && isIdentifierByte(text[pos - 1]))
                    --pos;
                start = pos;
                state = After::Name;
                continue;
            }
            if (c == ')' || c == ']') {
                state = After::Connector;
                continue; // the next group, read as an operand
            }
            if (c == '>') { // "make<int>(x)"
                const int opener = angleOpenerOf(text, pos - 1, limit);
                if (opener > limit && isIdentifierByte(text[opener - 1])) {
                    pos = opener;
                    while (pos > limit && isIdentifierByte(text[pos - 1]))
                        --pos;
                    start = pos;
                    state = After::Name;
                    continue;
                }
            }
        }
        // A member access before the operand.
        if (c == '.' && !(pos >= 2 && text[pos - 2] == '.')) {
            pos -= (pos >= 2 && text[pos - 2] == '?') ? 2 : 1;
        } else if (pos >= 2 && text[pos - 2] == '-' && c == '>') {
            pos -= 2;
        } else if (pos >= 2 && text[pos - 2] == ':' && c == ':') {
            pos -= 2;
            scopeConnector = true;
            state = After::Connector;
            continue;
        } else {
            break;
        }
        scopeConnector = false;
        state = After::Connector;
    }
    if (state == After::Connector && scopeConnector)
        start = pos; // "::global"
    if (start < 0 || start >= end)
        return -1;
    // "new Foo()": the object created, not the constructor's result.
    int before = start;
    while (before > 0 && text[before - 1] == ' ')
        --before;
    if (before >= 3 && before < start && text.mid(before - 3, 3) == "new" &&
        (before == 3 || !isIdentifierByte(text[before - 4])))
        start = before - 3;
    return start;
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
    const auto skipBlanks = [&text, limit](int i) {
        while (i > limit && (text[i - 1] == ' ' || text[i - 1] == '\t' || text[i - 1] == '\n' || text[i - 1] == '\r'))
            --i;
        return i;
    };
    while (starts.size() < maxExpressionChoices) {
        const int i = skipBlanks(start);
        // "!ready.if": the negation too.
        if (i > limit && text[i - 1] == '!' && !(i < text.size() && text[i] == '=')) {
            start = i - 1;
            starts.append(start);
            continue;
        }
        // A binary operator, then its left operand: "a + b.if".
        int length = 0;
        if (i - 2 >= limit) {
            const QByteArray two = text.mid(i - 2, 2);
            if (two == "&&" || two == "||" || two == "==" || two == "!=" || two == "<=" || two == ">=" || two == "<<" ||
                two == ">>")
                length = 2;
            else if (two == "++" || two == "--" || two == "->" || two == "::" || (two[1] == '=' && two[0] != ' '))
                break; // an assignment, an increment
        }
        if (length == 0 && i - 1 >= limit && QByteArrayView("+-*/%<>&|^").contains(text[i - 1]))
            length = 1;
        if (length == 0)
            break;
        const int operandEnd = skipBlanks(i - length);
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
    QString text = expression.trimmed();
    // Arguments and subscripts at the end go.
    while (text.endsWith(QLatin1Char(')')) || text.endsWith(QLatin1Char(']'))) {
        const QByteArray bytes = text.toUtf8();
        const int opener = openerOf(bytes, int(bytes.size()) - 1, 0);
        if (opener < 0)
            break;
        text = QString::fromUtf8(bytes.left(opener)).trimmed();
    }
    QString name = trailing.match(text).captured(1);
    for (const QLatin1String prefix : {QLatin1String("get"), QLatin1String("is"), QLatin1String("to")}) {
        if (name.size() > prefix.size() && name.startsWith(prefix)) {
            const QChar next = name[prefix.size()];
            if (next.isUpper()) {
                name = name.mid(prefix.size());
                break;
            }
            if (next == QLatin1Char('_') && name.size() > prefix.size() + 1) {
                name = name.mid(prefix.size() + 1);
                break;
            }
        }
    }
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

void PostfixTemplateProvider::complete(const CompletionContext &context, const Callback &done)
{
    const int dot = context.prefixStart - 1;
    const QList<int> starts = expressionStarts(context.text, dot);
    // Nothing before the dot, or a number ("3.f").
    if (starts.isEmpty() || (context.text[starts.first()] >= '0' && context.text[starts.first()] <= '9')) {
        done({}, false);
        return;
    }
    const unsigned language = languageOf(context.languageId);
    int end = dot;
    if (context.text[end - 1] == '?')
        --end;
    const QString unit = CodeScan::indentUnit(context.text);
    const QString postfix = QObject::tr("Postfix template");

    // Each expression the template can wrap: the nearest first.
    struct Candidate {
        int start;
        QString expression;
        bool compound; // "a + b": parenthesized where it is an operand
        bool statement;
        QString indent;
        QString name;
    };

    QList<Candidate> candidates;
    for (int i = 0; i < starts.size(); ++i) {
        const QString expression = QString::fromUtf8(context.text.mid(starts[i], end - starts[i]));
        candidates.append({starts[i], expression, i > 0 && !expression.startsWith(QLatin1Char('!')),
                           startsStatement(context.text, starts[i]), CodeScan::lineIndent(context.text, starts[i]),
                           suggestName(expression)});
    }

    QList<CompletionItem> items;
    for (const Template &entry : postfixTemplates()) {
        if (!(entry.languages & language))
            continue;
        // "((type) a + b)" and "a + b.begin()" need the parentheses.
        static const QSet<QString> operands = {"cast", "begin", "cbegin", "co_await", "co_yield", "await", "new"};
        const bool operand = operands.contains(QString::fromLatin1(entry.key));
        const auto fill = [&entry, &unit, operand](const Candidate &candidate, const QString &value) {
            QString snippet = CodeScan::indented(QString::fromUtf8(entry.body), candidate.indent, unit);
            const QString wrapped = candidate.compound ? QLatin1Char('(') + value + QLatin1Char(')') : value;
            snippet.replace(QChar(0x00AC), Snippet::escape(QLatin1Char('!') + wrapped)); // ¬
            snippet.replace(QChar(0x00B6), Snippet::escape(quoted(value)));              // ¶
            snippet.replace(QChar(0x00A7), Snippet::escape(operand ? wrapped : value));  // §
            snippet.replace(QChar(0x00A4), candidate.name);                              // ¤
            return snippet;
        };
        QList<const Candidate *> fitting;
        for (const Candidate &candidate : candidates) {
            if (!entry.statement || candidate.statement)
                fitting.append(&candidate);
        }
        if (fitting.isEmpty())
            continue;
        const Candidate &nearest = *fitting.first();
        CompletionItem item =
            itemOf(entry, fill(nearest, nearest.expression), fill(nearest, QStringLiteral("expr")), postfix, 3);
        item.replaceStart = nearest.start;
        if (entry.import)
            item.additionalEdits = javaImport(context.text, QString::fromLatin1(entry.import));
        if (fitting.size() > 1) {
            for (const Candidate *candidate : fitting) {
                QString label = candidate->expression.simplified();
                if (label.size() > 60)
                    label = label.left(59) + QChar(0x2026);
                item.choices.append({label, fill(*candidate, candidate->expression), candidate->start});
            }
        }
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
        if (result.method.name.isEmpty()) {
            const int start = CodeScan::headerStart(blanked, brace);
            const CodeScan::Function function =
                CodeScan::parseFunction(QString::fromUtf8(blanked.mid(start, brace - start)).trimmed());
            static const QSet<QString> statements = {"if", "for", "while", "switch", "catch", "synchronized", "try"};
            if (!function.name.isEmpty() && !statements.contains(function.name)) {
                result.method = function;
                bodyStart = std::max(bodyStart, brace + 1);
            }
        }
    }
    // "Type name =", "Type name;", "for (Type name :": the last one.
    static const QRegularExpression declaration(
        QStringLiteral("\\b([A-Za-z_][\\w.]*(?:<[^;{}()=]*>)?(?:\\[\\])*)\\s+([A-Za-z_]\\w*)\\s*(?:=(?!=)|;|:(?!:))"));
    static const QSet<QString> notTypes = {"return", "new", "throw", "else", "case", "package", "import", "yield"};
    const QString body = QString::fromUtf8(blanked.mid(bodyStart, position - bodyStart));
    for (QRegularExpressionMatchIterator it = declaration.globalMatch(body); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        if (!notTypes.contains(match.captured(1)))
            result.variable = match.captured(2);
    }
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
    const QString live = QObject::tr("Live template");
    std::optional<Surroundings> around; // read for the first template that needs it
    for (const Template &entry : liveTemplates()) {
        if (!(entry.languages & language) || (entry.statement && !statement))
            continue;
        QString snippet = CodeScan::indented(QString::fromUtf8(entry.body), indent, unit);
        if (snippet.contains(QLatin1String("{{"))) {
            if (!around)
                around = surroundings(context.text, context.prefixStart);
            if (snippet.contains(QLatin1String("{{class}}")) && around->className.isEmpty())
                continue;
            if (snippet.contains(QLatin1String("{{method}}")) && around->method.name.isEmpty())
                continue;
            snippet.replace(QLatin1String("{{class}}"), Snippet::escape(around->className))
                .replace(QLatin1String("{{method}}"), Snippet::escape(around->method.name))
                .replace(QLatin1String("{{parameters}}"), Snippet::escape(printedParameters(around->method)))
                .replace(QLatin1String("{{variable}}"), Snippet::escape(around->variable));
        }
        // Below the keyword of the same name ("for").
        CompletionItem item = itemOf(entry, snippet, snippet, live, 1);
        if (entry.import)
            item.additionalEdits = javaImport(context.text, QString::fromLatin1(entry.import));
        items.append(item);
    }
    done(items, false);
}
