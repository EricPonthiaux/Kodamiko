#include "Templates.h"

#include "Snippet.h"

#include <QRegularExpression>

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
// negation, "¤" for a name suggested for its value; each tab is one
// indentation step of the file.
struct Template {
    const char *key;
    unsigned languages;
    bool statement; // only where a statement starts
    const char *body;
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
        {"log", Js, true, "console.log($0);"},
        {"fori", Js, true, "for (let ${1:i} = 0; $1 < ${2:n}; $1++) {\n\t$0\n}"},
        {"iter", Js, true, "for (const ${2:item} of ${1:items}) {\n\t$0\n}"},
        {"forin", Js, true, "for (const ${2:key} in ${1:object}) {\n\t$0\n}"},
        {"arf", Js, false, "(${1}) => {\n\t$0\n}"},
        {"imp", Js, true, "import ${2:name} from '${1:module}';$0"},
        {"tryc", Js, true, "try {\n\t$0\n} catch (${1:error}) {\n}"},
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

bool isIdentifierByte(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$'
        || static_cast<unsigned char>(c) >= 0x80;
}

// How far back an expression is looked for: a call's arguments rarely span
// more.
constexpr int maxExpressionBytes = 4000;

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

// Whether a statement may start at `at`: only blanks before it on the
// line, or after a "{", "}" or ";".
bool startsStatement(const QByteArray &text, int at)
{
    int i = at - 1;
    while (i >= 0 && (text[i] == ' ' || text[i] == '\t'))
        --i;
    return i < 0 || QByteArrayView("\n{};").contains(text[i]);
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

// One indentation step of the file: a tab if its lines start with tabs,
// else the smallest indentation in spaces (4 when nothing tells).
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

QString escapeSnippet(QString text)
{
    text.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    text.replace(QLatin1Char('$'), QLatin1String("\\$"));
    text.replace(QLatin1Char('}'), QLatin1String("\\}"));
    return text;
}

// `body` for its place: each line after the first at the indentation of
// the first, each tab one step of the file.
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

// The candidate of a template: matched on its key, described by what it
// gives (with "expr" for the expression), showing the real result beside
// the list.
CompletionItem itemOf(const Template &entry, const QString &snippet, const QString &sample, const QString &kindLabel,
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
    item.detail = detail;
    item.documentation = kindLabel + QStringLiteral("\n\n") + Snippet::expand(snippet).text;
    return item;
}

} // namespace

// PostfixTemplateProvider

int PostfixTemplateProvider::expressionStart(const QByteArray &text, int dot)
{
    if (dot <= 0 || dot >= text.size() || text[dot] != '.')
        return -1;
    const int limit = std::max(0, dot - maxExpressionBytes);
    int end = dot;
    if (end > 0 && text[end - 1] == '?') // "a?.b"
        --end;
    enum class After { Connector, Name, Group };
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
    if (before >= 3 && before < start && text.mid(before - 3, 3) == "new"
        && (before == 3 || !isIdentifierByte(text[before - 4])))
        start = before - 3;
    return start;
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
    return languageOf(context.languageId) != 0 && context.prefixStart > 0
        && context.prefixStart <= context.text.size() && context.text[context.prefixStart - 1] == '.';
}

QString PostfixTemplateProvider::triggerCharacters(const QString &languageId) const
{
    return languageOf(languageId) != 0 ? QStringLiteral(".") : QString();
}

void PostfixTemplateProvider::complete(const CompletionContext &context, const Callback &done)
{
    const int dot = context.prefixStart - 1;
    const int start = expressionStart(context.text, dot);
    // Nothing before the dot, or a number ("3.f").
    if (start < 0 || (context.text[start] >= '0' && context.text[start] <= '9')) {
        done({}, false);
        return;
    }
    const unsigned language = languageOf(context.languageId);
    int end = dot;
    if (context.text[end - 1] == '?')
        --end;
    const QString expression = QString::fromUtf8(context.text.mid(start, end - start));
    const bool statement = startsStatement(context.text, start);
    const QString indent = lineIndent(context.text, start);
    const QString unit = indentUnit(context.text);
    const QString name = suggestName(expression);
    const QString postfix = QObject::tr("Postfix template");

    QList<CompletionItem> items;
    for (const Template &entry : postfixTemplates()) {
        if (!(entry.languages & language) || (entry.statement && !statement))
            continue;
        const QString body = indented(QString::fromUtf8(entry.body), indent, unit);
        const auto fill = [&body, &name](const QString &value) {
            QString snippet = body;
            snippet.replace(QChar(0x00AC), escapeSnippet(QLatin1Char('!') + value)); // ¬
            snippet.replace(QChar(0x00A7), escapeSnippet(value));                    // §
            snippet.replace(QChar(0x00A4), name);                                    // ¤
            return snippet;
        };
        CompletionItem item = itemOf(entry, fill(expression), fill(QStringLiteral("expr")), postfix, 3);
        item.replaceStart = start;
        items.append(item);
    }
    done(items, false);
}

// LiveTemplateProvider

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
    const QString indent = lineIndent(context.text, context.prefixStart);
    const QString unit = indentUnit(context.text);
    const QString live = QObject::tr("Live template");
    for (const Template &entry : liveTemplates()) {
        if (!(entry.languages & language) || (entry.statement && !statement))
            continue;
        const QString snippet = indented(QString::fromUtf8(entry.body), indent, unit);
        // Below the keyword of the same name ("for").
        items.append(itemOf(entry, snippet, snippet, live, 1));
    }
    done(items, false);
}
