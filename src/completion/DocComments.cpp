#include "DocComments.h"

#include "CodeScan.h"
#include "Snippet.h"

#include <QObject>
#include <QRegularExpression>
#include <QSet>

#include <optional>

namespace {

enum class Style {
    None,
    Doxygen,
    Javadoc,
    JsDoc
};

Style styleOf(const QString &languageId)
{
    if (languageId == QLatin1String("c") || languageId == QLatin1String("cpp"))
        return Style::Doxygen;
    if (languageId == QLatin1String("java"))
        return Style::Javadoc;
    if (languageId == QLatin1String("javascript") || languageId == QLatin1String("typescript") ||
        languageId == QLatin1String("tsx"))
        return Style::JsDoc;
    return Style::None;
}

struct Tag {
    const char *name;
    const char *detail;
};

const QList<Tag> &tagsOf(Style style)
{
    static const QList<Tag> doxygen = {
        {"brief", "summary"},
        {"param", "a parameter"},
        {"tparam", "a template parameter"},
        {"return", "the result"},
        {"returns", "the result"},
        {"retval", "a returned value"},
        {"throws", "an exception"},
        {"exception", "an exception"},
        {"note", "a note"},
        {"warning", "a warning"},
        {"see", "a reference"},
        {"sa", "see also"},
        {"details", "the description"},
        {"pre", "a precondition"},
        {"post", "a postcondition"},
        {"invariant", "an invariant"},
        {"deprecated", "why"},
        {"since", "a version"},
        {"todo", "work left"},
        {"code", "a code block"},
        {"endcode", "end of the code block"},
        {"p", "a parameter's name"},
        {"a", "in italics"},
        {"c", "as code"},
        {"ref", "a link"},
        {"file", "the file"},
        {"author", "an author"},
        {"version", "a version"},
        {"copydoc", "copy a documentation"},
        {"internal", "not public"},
        {"attention", "a remark"},
        {"example", "an example"},
        {"overload", "an overload"},
    };
    static const QList<Tag> javadoc = {
        {"param", "a parameter"},
        {"return", "the result"},
        {"throws", "an exception"},
        {"exception", "an exception"},
        {"see", "a reference"},
        {"since", "a version"},
        {"deprecated", "why"},
        {"author", "an author"},
        {"version", "a version"},
        {"link", "a link"},
        {"linkplain", "a plain link"},
        {"code", "as code"},
        {"inheritDoc", "the overridden method's"},
        {"literal", "as text"},
        {"value", "a constant's value"},
        {"serial", "a serialized field"},
        {"apiNote", "a note"},
        {"implNote", "an implementation note"},
    };
    static const QList<Tag> jsdoc = {
        {"param", "a parameter"},
        {"returns", "the result"},
        {"type", "the type"},
        {"typedef", "a type"},
        {"property", "a property"},
        {"example", "an example"},
        {"throws", "an exception"},
        {"deprecated", "why"},
        {"see", "a reference"},
        {"link", "a link"},
        {"async", "asynchronous"},
        {"template", "a type parameter"},
        {"private", "not public"},
        {"public", "public"},
        {"override", "an override"},
        {"default", "the default value"},
        {"since", "a version"},
        {"author", "an author"},
        {"callback", "a callback type"},
        {"fires", "an event"},
        {"module", "the module"},
    };
    static const QList<Tag> none;
    switch (style) {
    case Style::Doxygen:
        return doxygen;
    case Style::Javadoc:
        return javadoc;
    case Style::JsDoc:
        return jsdoc;
    case Style::None:
        break;
    }
    return none;
}

// Where the declaration documented by the comment at `start` begins:
// after its "*/", or after its last "///" line.
int afterComment(const QByteArray &text, int start)
{
    if (text.mid(start, 2) == "/*") {
        const int end = int(text.indexOf("*/", start + 2));
        return end < 0 ? -1 : end + 2;
    }
    int lineEnd = int(text.indexOf('\n', start));
    while (lineEnd >= 0) {
        int next = lineEnd + 1;
        while (next < text.size() && (text[next] == ' ' || text[next] == '\t'))
            ++next;
        if (text.mid(next, 3) != "///" && text.mid(next, 3) != "//!")
            return lineEnd + 1;
        lineEnd = int(text.indexOf('\n', next));
    }
    return int(text.size());
}

struct Documented {
    QStringList parameters;
    QStringList templateParameters;
    bool returnsValue = false;
    QStringList exceptions;
    bool found = false;
};

// "typename T", "int N = 3", "class... Ts": the name.
QString templateParameterName(const QString &declaration)
{
    QString text = declaration.section(QLatin1Char('='), 0, 0).trimmed();
    int start = int(text.size());
    while (start > 0 && (text[start - 1].isLetterOrNumber() || text[start - 1] == u'_'))
        --start;
    return text.mid(start);
}

// The names of template parameters: "typename T, int N" gives T, N.
QStringList templateParameterNames(const QString &parameters)
{
    QStringList names;
    for (const QString &part : CodeScan::splitTopLevel(parameters)) {
        if (const QString name = templateParameterName(part); !name.isEmpty())
            names.append(name);
    }
    return names;
}

// The function declared by `header`; in JavaScript also "const f = (a, b)
// =>", "f = async function (a)". Nullopt when it declares none.
std::optional<CodeScan::Function> documentedFunction(const QString &header, Style style)
{
    CodeScan::Function function = CodeScan::parseFunction(header);
    if (function.name.isEmpty() && style == Style::JsDoc) {
        static const QRegularExpression arrow(
            QStringLiteral("=\\s*(?:async\\s+)?(?:function\\s*\\w*\\s*)?\\(([^()]*)\\)"));
        if (const QRegularExpressionMatch match = arrow.match(header); match.hasMatch()) {
            function.name = QStringLiteral("function");
            function.parameters = match.captured(1);
        }
    }
    if (function.name.isEmpty())
        return std::nullopt;
    return function;
}

bool returnsValue(const CodeScan::Function &function, Style style)
{
    // TypeScript: "f(): Type".
    if (style == Style::JsDoc)
        return function.trailing.startsWith(QLatin1Char(':')) && !function.trailing.contains(QLatin1String("void"));
    // Not a constructor (no type) nor a destructor.
    return function.returnsValue() && !function.name.startsWith(QLatin1Char('~'));
}

// Java's "throws IOException, Foo": the exceptions.
QStringList thrownExceptions(const CodeScan::Function &function)
{
    static const QRegularExpression throwsClause(QStringLiteral("\\bthrows\\s+([\\w.,\\s<>]+)"));
    const QRegularExpressionMatch match = throwsClause.match(function.trailing);
    if (!match.hasMatch())
        return {};
    QStringList exceptions;
    for (const QString &exception : match.captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts))
        exceptions.append(exception.trimmed());
    return exceptions;
}

Documented documentedAt(const QByteArray &text, int position, Style style)
{
    Documented result;
    const QString header = CodeScan::nextDeclaration(text, position);
    if (header.isEmpty())
        return result;
    static const QRegularExpression templateHead(
        QStringLiteral("^\\s*template\\s*<(.*?)>\\s*(?:class|struct|union|using)\\b"));
    if (const QRegularExpressionMatch match = templateHead.match(header); match.hasMatch()) {
        // A class template: its parameters only.
        result.templateParameters = templateParameterNames(match.captured(1));
        result.found = true;
        return result;
    }
    const std::optional<CodeScan::Function> function = documentedFunction(header, style);
    if (!function)
        return result;
    result.found = true;
    const bool typed = style != Style::JsDoc;
    for (const CodeScan::Parameter &parameter : function->parameterList(typed)) {
        if (!parameter.name.isEmpty())
            result.parameters.append(parameter.name);
    }
    result.templateParameters = templateParameterNames(function->templateParameters);
    result.returnsValue = returnsValue(*function, style);
    result.exceptions = thrownExceptions(*function);
    return result;
}

QString parameterTypeOf(const QByteArray &text, int position, const QString &name, bool typed)
{
    const CodeScan::Function function = CodeScan::parseFunction(CodeScan::nextDeclaration(text, position));
    for (const CodeScan::Parameter &parameter : function.parameterList(typed)) {
        if (parameter.name == name)
            return parameter.type;
    }
    return {};
}

// The start of the "///" or "//!" comment on the line of `position`, -1
// if none.
int lineCommentStart(const QByteArray &text, int position)
{
    int lineStart = position;
    while (lineStart > 0 && text[lineStart - 1] != '\n')
        --lineStart;
    int first = lineStart;
    while (first < position && (text[first] == ' ' || text[first] == '\t'))
        ++first;
    const bool isDocLine = text.mid(first, 3) == "///" || text.mid(first, 3) == "//!";
    const bool isDocComment = position >= first + 3 && isDocLine && text.mid(first, 4) != "////";
    return isDocComment ? first : -1;
}

// Whether the "/*" at `open` opens a documentation comment: "/**" or
// "/*!", not "/**/" nor in a line comment ("// see /*").
bool opensDocComment(const QByteArray &text, int open)
{
    const char marker = text[open + 2];
    if ((marker != '*' && marker != '!') || text.mid(open, 4) == "/**/")
        return false;
    const int openLine = int(text.lastIndexOf('\n', open)) + 1;
    return !text.mid(openLine, open - openLine).contains("//");
}

QString tagKindName(Style style)
{
    switch (style) {
    case Style::Doxygen:
        return QObject::tr("Doxygen command");
    case Style::Javadoc:
        return QObject::tr("Javadoc tag");
    case Style::JsDoc:
    case Style::None:
        break;
    }
    return QObject::tr("JSDoc tag");
}

// The tags of the comment's style, the usual ones first.
QList<CompletionItem> tagItems(Style style)
{
    const QString kind = tagKindName(style);
    const QList<Tag> &tags = tagsOf(style);
    QList<CompletionItem> items;
    for (int i = 0; i < tags.size(); ++i) {
        CompletionItem item;
        item.label = QString::fromLatin1(tags[i].name);
        item.detail = QString::fromLatin1(tags[i].detail);
        item.kind = CompletionItem::Kind::Keyword;
        item.documentation = kind;
        item.order = i;
        item.priority = 2;
        items.append(item);
    }
    return items;
}

// The names `comment` documents already with a "@param" tag, the one
// being typed at `prefixInComment` left out.
QSet<QString> documentedNamesIn(const QString &comment, int prefixInComment)
{
    static const QRegularExpression documentedTag(QStringLiteral(
        "[@\\\\](?:param|arg|argument|tparam|template)(?:\\[[^\\]]*\\])?\\s+(?:\\{[^}]*\\}\\s*)?([\\w$]+)"));
    QSet<QString> names;
    for (QRegularExpressionMatchIterator it = documentedTag.globalMatch(comment); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        if (match.capturedStart(1) != prefixInComment)
            names.insert(match.captured(1));
    }
    return names;
}

// After "@param" ("@param[in]", "@param {Type}") in the comment starting
// at `commentStart`: the parameters still undocumented, in the order of
// the declaration.
QList<CompletionItem> parameterItems(const CompletionContext &context, int commentStart, Style style)
{
    const QByteArray &text = context.text;
    int lineStart = context.prefixStart;
    while (lineStart > 0 && text[lineStart - 1] != '\n')
        --lineStart;
    const QString line = QString::fromUtf8(text.mid(lineStart, context.prefixStart - lineStart));
    static const QRegularExpression paramTag(
        QStringLiteral("[@\\\\](param|arg|argument|tparam|template)(?:\\[[^\\]]*\\])?\\s+(?:\\{[^}]*\\}\\s*)?$"));
    const QRegularExpressionMatch tag = paramTag.match(line);
    const int after = afterComment(text, commentStart);
    if (!tag.hasMatch() || after < 0)
        return {};
    const bool templates = tag.captured(1) == QLatin1String("tparam") || tag.captured(1) == QLatin1String("template");
    const Documented documented = documentedAt(text, after, style);
    const QString comment = QString::fromUtf8(text.mid(commentStart, after - commentStart));
    const int prefixInComment =
        int(QString::fromUtf8(text.mid(commentStart, context.prefixStart - commentStart)).size());
    const QSet<QString> documentedNames = documentedNamesIn(comment, prefixInComment);
    const QStringList names = templates ? documented.templateParameters : documented.parameters;
    QList<CompletionItem> items;
    for (int i = 0; i < names.size(); ++i) {
        if (documentedNames.contains(names[i]))
            continue;
        CompletionItem item;
        item.label = names[i];
        item.kind = templates ? CompletionItem::Kind::Type : CompletionItem::Kind::Variable;
        if (!templates)
            item.detail = parameterTypeOf(text, after, names[i], style != Style::JsDoc);
        item.order = i;
        item.priority = 10;
        items.append(item);
    }
    return items;
}

} // namespace

namespace DocComments {

int commentStart(const QByteArray &text, int position, const QString &languageId)
{
    if (styleOf(languageId) == Style::None || position <= 0 || position > text.size())
        return -1;
    if (const int lineComment = lineCommentStart(text, position); lineComment >= 0)
        return lineComment;
    const int open = int(text.lastIndexOf("/*", position - 2));
    if (open < 0 || open + 2 >= text.size())
        return -1;
    const int close = int(text.lastIndexOf("*/", position - 2));
    if (close > open || !opensDocComment(text, open))
        return -1;
    return open;
}

QString stub(const QByteArray &text, int position, const QString &languageId, const QString &indent)
{
    const Style style = styleOf(languageId);
    const Documented documented = documentedAt(text, position, style);
    const QString line = QLatin1Char('\n') + indent + QStringLiteral(" *");
    QStringList tags;
    int stop = 2;
    for (const QString &name : documented.templateParameters)
        tags.append(QStringLiteral(" @%1 %2 ${%3}")
                        .arg(style == Style::JsDoc ? QStringLiteral("template") : QStringLiteral("tparam"),
                             Snippet::escape(name))
                        .arg(stop++));
    for (const QString &name : documented.parameters)
        tags.append(QStringLiteral(" @param %1 ${%2}").arg(Snippet::escape(name)).arg(stop++));
    if (documented.returnsValue)
        tags.append(QStringLiteral(" @%1 ${%2}")
                        .arg(style == Style::JsDoc ? QStringLiteral("returns") : QStringLiteral("return"))
                        .arg(stop++));
    for (const QString &exception : documented.exceptions)
        tags.append(QStringLiteral(" @throws %1 ${%2}").arg(Snippet::escape(exception)).arg(stop++));
    QString snippet = line + QStringLiteral(" ${1}");
    if (!tags.isEmpty())
        snippet += line;
    for (const QString &tag : tags)
        snippet += line + tag;
    snippet += QLatin1Char('\n') + indent + QStringLiteral(" */");
    return snippet;
}

} // namespace DocComments

bool DocCommentProvider::handles(const CompletionContext &context) const
{
    return context.inDocComment && styleOf(context.languageId) != Style::None;
}

void DocCommentProvider::complete(const CompletionContext &context, const Callback &done)
{
    const QByteArray &text = context.text;
    const int start = DocComments::commentStart(text, context.position, context.languageId);
    if (start < 0 || context.prefixStart <= 0) {
        done({}, false);
        return;
    }
    const Style style = styleOf(context.languageId);
    const char before = text[context.prefixStart - 1];
    const bool afterTagMarker = before == '@' || (before == '\\' && style == Style::Doxygen);
    done(afterTagMarker ? tagItems(style) : parameterItems(context, start, style), false);
}
