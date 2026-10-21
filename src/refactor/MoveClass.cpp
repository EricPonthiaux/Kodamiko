// SPDX-License-Identifier: GPL-3.0-only

#include "MoveClass.h"

#include "syntax/TreeSitterGrammars.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStringList>

#include <tree_sitter/api.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <ranges>
#include <string_view>

using namespace LspProtocol;

namespace MoveClass {

namespace {

using Tree = std::unique_ptr<TSTree, decltype(&ts_tree_delete)>;

// A file's UTF-8 bytes and their syntax tree.
struct Parsed {
    QByteArray bytes;
    Tree tree{nullptr, ts_tree_delete};
};

// A class, struct, interface… declared outside any other, and the
// namespaces it is in, outermost first.
struct TopLevel {
    TSNode node; // the class, or the template declaring it
    QString name;
    QStringList namespaces;
};

// A part of a file, in bytes.
struct Extent {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
};

// What a new file is made of.
struct NewFile {
    QString leadingComment;  // the license
    QString guardLine;       // "#pragma once"…, C++ headers only
    QStringList headerLines; // package, imports, includes
    QStringList namespaces;
    QStringList bodies;
};

QString tr(const char *text)
{
    return QCoreApplication::translate("MoveClass", text);
}

std::string_view typeOf(TSNode node)
{
    return ts_node_type(node);
}

QString textOf(const QByteArray &bytes, std::uint32_t start, std::uint32_t end)
{
    return QString::fromUtf8(bytes.constData() + start, qsizetype(end) - start);
}

QString textOf(const QByteArray &bytes, TSNode node)
{
    return textOf(bytes, ts_node_start_byte(node), ts_node_end_byte(node));
}

bool isIdentifierByte(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// `word` replaced by `replacement`, of the same length, wherever it is a
// whole word: the positions do not move.
void replaceWord(QByteArray &text, const QByteArray &word, const QByteArray &replacement)
{
    for (qsizetype at = text.indexOf(word); at >= 0; at = text.indexOf(word, at + word.size())) {
        const qsizetype end = at + word.size();
        const bool startsWord = at == 0 || !isIdentifierByte(text[at - 1]);
        const bool endsWord = end >= text.size() || !isIdentifierByte(text[end]);
        if (startsWord && endsWord)
            text.replace(at, word.size(), replacement);
    }
}

// What tree-sitter reads of a C++ text: Qt's class macros blanked and
// "signals:" read as "public:", which would otherwise break the class.
QByteArray parsableCpp(QByteArray text)
{
    for (const QByteArrayView macro : {"Q_OBJECT", "Q_GADGET", "Q_NAMESPACE"})
        replaceWord(text, macro.toByteArray(), QByteArray(macro.size(), ' '));
    replaceWord(text, "signals", "public ");
    replaceWord(text, "slots", "     ");
    return text;
}

Parsed parse(const QString &languageId, const QString &text)
{
    Parsed parsed;
    parsed.bytes = text.toUtf8();
    const QByteArray parsable = languageId == QLatin1String("cpp") ? parsableCpp(parsed.bytes) : parsed.bytes;
    const TreeSitterGrammar *grammar = TreeSitterGrammars::forLanguage(languageId);
    if (!grammar)
        return parsed;
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, grammar->language);
    parsed.tree.reset(ts_parser_parse_string(parser, nullptr, parsable.constData(), std::uint32_t(parsable.size())));
    ts_parser_delete(parser);
    return parsed;
}

Position positionAt(const QByteArray &bytes, std::uint32_t offset)
{
    const qsizetype lineStart = bytes.lastIndexOf('\n', qsizetype(offset) - 1) + 1;
    const int line = int(bytes.left(offset).count('\n'));
    return {line, int(textOf(bytes, std::uint32_t(lineStart), offset).size())};
}

std::uint32_t offsetAt(const QByteArray &bytes, const Position &position)
{
    qsizetype lineStart = 0;
    for (int line = 0; line < position.line && lineStart >= 0; ++line) {
        lineStart = bytes.indexOf('\n', lineStart);
        if (lineStart >= 0)
            ++lineStart;
    }
    if (lineStart < 0)
        return std::uint32_t(bytes.size());
    qsizetype lineEnd = bytes.indexOf('\n', lineStart);
    if (lineEnd < 0)
        lineEnd = bytes.size();
    const QString line = textOf(bytes, std::uint32_t(lineStart), std::uint32_t(lineEnd));
    return std::uint32_t(lineStart + line.left(position.character).toUtf8().size());
}

// The declarations of `container`, those of its #if and #ifdef blocks
// (an include guard) included.
QList<TSNode> declarationsIn(TSNode container)
{
    QList<TSNode> declarations;
    for (uint32_t i = 0; i < ts_node_named_child_count(container); ++i) {
        const TSNode child = ts_node_named_child(container, i);
        const bool isConditional = typeOf(child) == "preproc_ifdef" || typeOf(child) == "preproc_if";
        if (isConditional)
            declarations.append(declarationsIn(child));
        else
            declarations.append(child);
    }
    return declarations;
}

bool isCppClass(TSNode node)
{
    const bool isClass = typeOf(node) == "class_specifier" || typeOf(node) == "struct_specifier";
    return isClass && !ts_node_is_null(ts_node_child_by_field_name(node, "body", 4));
}

bool isJavaClass(TSNode node)
{
    const std::string_view type = typeOf(node);
    return type == "class_declaration" || type == "interface_declaration" || type == "enum_declaration" ||
           type == "record_declaration";
}

QString nameOf(const QByteArray &bytes, TSNode node)
{
    const TSNode name = ts_node_child_by_field_name(node, "name", 4);
    return ts_node_is_null(name) ? QString() : textOf(bytes, name);
}

// The C++ class a template declares, if it declares one.
std::optional<TSNode> templatedClass(TSNode node)
{
    for (uint32_t i = 0; i < ts_node_named_child_count(node); ++i) {
        const TSNode child = ts_node_named_child(node, i);
        if (isCppClass(child))
            return child;
    }
    return std::nullopt;
}

// C's "typedef struct Point { … } Point;": the typedef's name.
std::optional<QString> typedefStructName(const QByteArray &bytes, TSNode node)
{
    if (typeOf(node) != "type_definition")
        return std::nullopt;
    const TSNode type = ts_node_child_by_field_name(node, "type", 4);
    const bool isStruct = !ts_node_is_null(type) &&
                          (isCppClass(type) || typeOf(type) == "union_specifier" || typeOf(type) == "enum_specifier");
    const TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
    if (!isStruct || ts_node_is_null(declarator))
        return std::nullopt;
    return textOf(bytes, declarator).trimmed();
}

void collectCppClasses(const QByteArray &bytes,
                       TSNode container,
                       const QStringList &namespaces,
                       QList<TopLevel> &classes)
{
    for (const TSNode child : declarationsIn(container)) {
        if (typeOf(child) == "namespace_definition") {
            const QString name = nameOf(bytes, child);
            const TSNode body = ts_node_child_by_field_name(child, "body", 4);
            if (!name.isEmpty() && !ts_node_is_null(body))
                collectCppClasses(bytes, body, namespaces + QStringList{name}, classes);
        } else if (isCppClass(child)) {
            classes.append({child, nameOf(bytes, child), namespaces});
        } else if (typeOf(child) == "template_declaration") {
            if (const std::optional<TSNode> declared = templatedClass(child))
                classes.append({child, nameOf(bytes, *declared), namespaces});
        } else if (const std::optional<QString> name = typedefStructName(bytes, child)) {
            classes.append({child, *name, namespaces});
        }
    }
}

QList<TopLevel> topLevelClasses(const QString &languageId, const Parsed &parsed)
{
    QList<TopLevel> classes;
    const TSNode root = ts_tree_root_node(parsed.tree.get());
    if (languageId == QLatin1String("cpp")) {
        collectCppClasses(parsed.bytes, root, {}, classes);
        return classes;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(root); ++i) {
        const TSNode child = ts_node_named_child(root, i);
        if (isJavaClass(child))
            classes.append({child, nameOf(parsed.bytes, child), {}});
    }
    return classes;
}

bool isComment(TSNode node)
{
    return typeOf(node).find("comment") != std::string_view::npos;
}

// `node` with the comments right above it, its ";" and its line ends,
// and one blank line after it.
Extent extentOf(const QByteArray &bytes, TSNode node)
{
    std::uint32_t start = ts_node_start_byte(node);
    for (TSNode previous = ts_node_prev_named_sibling(node); !ts_node_is_null(previous) && isComment(previous);
         previous = ts_node_prev_named_sibling(previous)) {
        const QString between = textOf(bytes, ts_node_end_byte(previous), start);
        if (between.count(QLatin1Char('\n')) > 1)
            break; // a blank line: the comment is about something else
        start = ts_node_start_byte(previous);
    }
    while (start > 0 && (bytes[start - 1] == ' ' || bytes[start - 1] == '\t'))
        --start;
    auto end = std::uint32_t(ts_node_end_byte(node));
    qsizetype next = end;
    while (next < bytes.size() && (bytes[next] == ' ' || bytes[next] == '\t'))
        ++next;
    if (next < bytes.size() && bytes[next] == ';')
        end = std::uint32_t(next + 1);
    for (int lineEnds = 0; lineEnds < 2 && end < bytes.size() && bytes[end] == '\n'; ++lineEnds)
        ++end;
    return {start, end};
}

// The comments the file starts with, before any code (its license).
QString leadingCommentOf(const Parsed &parsed)
{
    const TSNode root = ts_tree_root_node(parsed.tree.get());
    std::uint32_t end = 0;
    for (uint32_t i = 0; i < ts_node_named_child_count(root); ++i) {
        const TSNode child = ts_node_named_child(root, i);
        if (!isComment(child))
            break;
        end = ts_node_end_byte(child);
    }
    return textOf(parsed.bytes, 0, end).trimmed();
}

QStringList textsOf(const Parsed &parsed, std::initializer_list<std::string_view> types)
{
    QStringList texts;
    for (const TSNode child : declarationsIn(ts_tree_root_node(parsed.tree.get()))) {
        if (std::ranges::find(types, typeOf(child)) != types.end())
            texts.append(textOf(parsed.bytes, child).trimmed());
    }
    return texts;
}

bool isHeader(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix();
    return suffix == QLatin1String("h") || suffix == QLatin1String("hpp") || suffix == QLatin1String("hh") ||
           suffix == QLatin1String("hxx");
}

// The guard of a new header: the moved class's header's style, or
// "#pragma once" for a class moved out of a source.
QString guardOf(const QString &path, const QString &text, const QString &className)
{
    if (!isHeader(path) || text.contains(QLatin1String("#pragma once")))
        return QStringLiteral("#pragma once");
    const QString guard = className.toUpper() + QStringLiteral("_H");
    return QStringLiteral("#ifndef %1\n#define %1").arg(guard);
}

QString compose(const NewFile &file, bool isGuarded)
{
    QStringList parts;
    if (!file.leadingComment.isEmpty())
        parts.append(file.leadingComment);
    if (!file.guardLine.isEmpty())
        parts.append(file.guardLine);
    if (!file.headerLines.isEmpty())
        parts.append(file.headerLines.join(QLatin1Char('\n')));
    QString body = file.bodies.join(QStringLiteral("\n\n"));
    for (const QString &name : std::views::reverse(file.namespaces))
        body = QStringLiteral("namespace %1 {\n\n%2\n\n} // namespace %1").arg(name, body);
    parts.append(body);
    if (isGuarded)
        parts.append(QStringLiteral("#endif"));
    return parts.join(QStringLiteral("\n\n")) + QLatin1Char('\n');
}

TextEdit deletion(const QByteArray &bytes, const Extent &extent)
{
    return {{positionAt(bytes, extent.start), positionAt(bytes, extent.end)}, {}};
}

TextEdit insertion(const QByteArray &bytes, std::uint32_t offset, const QString &text)
{
    const Position at = positionAt(bytes, offset);
    return {{at, at}, text};
}

// The start of the line after `offset`.
std::uint32_t nextLine(const QByteArray &bytes, qsizetype offset)
{
    const qsizetype lineEnd = bytes.indexOf('\n', offset);
    return std::uint32_t(lineEnd < 0 ? bytes.size() : lineEnd + 1);
}

// After the file's last include, else after its guard, else after its
// leading comment.
std::uint32_t includeOffsetOf(const Parsed &parsed)
{
    std::uint32_t lastInclude = 0;
    for (const TSNode child : declarationsIn(ts_tree_root_node(parsed.tree.get()))) {
        if (typeOf(child) == "preproc_include")
            lastInclude = ts_node_start_byte(child);
    }
    if (lastInclude > 0 || parsed.bytes.startsWith("#include"))
        return nextLine(parsed.bytes, lastInclude);
    for (const char *guard : {"#pragma once", "#define"}) {
        const qsizetype found = parsed.bytes.indexOf(guard);
        if (found >= 0)
            return nextLine(parsed.bytes, found);
    }
    const QString comment = leadingCommentOf(parsed);
    return comment.isEmpty() ? 0
                             : nextLine(parsed.bytes, parsed.bytes.indexOf(comment.toUtf8()) + comment.toUtf8().size());
}

QList<DocumentChange> newFile(const QString &path, const QString &text)
{
    return {FileOperation{.kind = FileOperation::Kind::Create, .filePath = path},
            DocumentEdit{path, std::nullopt, {{{{0, 0}, {0, 0}}, text}}}};
}

QString siblingPath(const QString &path, const QString &name, const QString &suffix)
{
    return QFileInfo(path).absolutePath() + QLatin1Char('/') + name + QLatin1Char('.') + suffix;
}

// A definition of a member of `className`: a function ("Total::value",
// "Total::~Total") or a static data member ("int Total::count = 0;").
bool definesMemberOf(const QByteArray &bytes, TSNode node, const QString &className)
{
    const TSNode definition = typeOf(node) == "template_declaration" ? ts_node_named_child(node, 1) : node;
    const bool isDefinition = typeOf(definition) == "function_definition" || typeOf(definition) == "declaration";
    if (!isDefinition)
        return false;
    const TSNode declarator = ts_node_child_by_field_name(definition, "declarator", 10);
    if (ts_node_is_null(declarator))
        return false;
    QString text = textOf(bytes, declarator).trimmed();
    while (text.startsWith(QLatin1Char('*')) || text.startsWith(QLatin1Char('&')))
        text = text.mid(1).trimmed();
    return text.startsWith(className + QStringLiteral("::"));
}

void collectMembers(const QByteArray &bytes, TSNode container, const QString &className, QList<TSNode> &members)
{
    for (const TSNode child : declarationsIn(container)) {
        if (typeOf(child) == "namespace_definition") {
            const TSNode body = ts_node_child_by_field_name(child, "body", 4);
            if (!ts_node_is_null(body))
                collectMembers(bytes, body, className, members);
        } else if (definesMemberOf(bytes, child, className)) {
            members.append(child);
        }
    }
}

// The members' definitions, from `input`'s source to one of the class's.
QList<DocumentChange> moveMembers(const Input &input, const TopLevel &moved, const QString &headerName)
{
    if (input.sourcePath.isEmpty())
        return {};
    const Parsed source = parse(input.languageId, input.sourceText);
    if (!source.tree)
        return {};
    QList<TSNode> members;
    collectMembers(source.bytes, ts_tree_root_node(source.tree.get()), moved.name, members);
    if (members.isEmpty())
        return {};
    NewFile file{.leadingComment = leadingCommentOf(source),
                 .headerLines = QStringList{QStringLiteral("#include \"%1\"").arg(headerName)} +
                                textsOf(source, {"preproc_include"}),
                 .namespaces = moved.namespaces};
    QList<TextEdit> deletions;
    for (const TSNode member : members) {
        const Extent extent = extentOf(source.bytes, member);
        file.bodies.append(textOf(source.bytes, extent.start, extent.end).trimmed());
        deletions.append(deletion(source.bytes, extent));
    }
    const QString path = siblingPath(input.sourcePath, moved.name, QFileInfo(input.sourcePath).suffix());
    return newFile(path, compose(file, false)) + QList<DocumentChange>{DocumentEdit{input.sourcePath, {}, deletions}};
}

Result planCpp(const Input &input, const Parsed &parsed, const TopLevel &moved)
{
    const QString suffix = isHeader(input.path) ? QFileInfo(input.path).suffix() : QStringLiteral("h");
    const QString headerPath = siblingPath(input.path, moved.name, suffix);
    const QString headerName = QFileInfo(headerPath).fileName();
    const QString guard = guardOf(input.path, input.text, moved.name);
    const Extent extent = extentOf(parsed.bytes, moved.node);
    const NewFile header{.leadingComment = leadingCommentOf(parsed),
                         .guardLine = guard,
                         .headerLines = textsOf(parsed, {"preproc_include"}),
                         .namespaces = moved.namespaces,
                         .bodies = {textOf(parsed.bytes, extent.start, extent.end).trimmed()}};
    const bool isGuarded = guard.startsWith(QLatin1String("#ifndef"));
    Result result{moved.name, headerPath, {}, {}};
    result.edit.changes = newFile(headerPath, compose(header, isGuarded));
    const QString include = QStringLiteral("#include \"%1\"\n").arg(headerName);
    result.edit.changes.append(DocumentEdit{
        input.path, {}, {insertion(parsed.bytes, includeOffsetOf(parsed), include), deletion(parsed.bytes, extent)}});
    result.edit.changes.append(moveMembers(input, moved, headerName));
    return result;
}

bool isPublic(const QByteArray &bytes, TSNode node)
{
    const TSNode modifiers = ts_node_named_child(node, 0);
    return typeOf(modifiers) == "modifiers" && textOf(bytes, modifiers).contains(QLatin1String("public"));
}

Result planJava(const Input &input, const Parsed &parsed, const TopLevel &moved)
{
    if (isPublic(parsed.bytes, moved.node))
        return {moved.name, {}, {}, tr("“%1” is the public class of its file").arg(moved.name)};
    const Extent extent = extentOf(parsed.bytes, moved.node);
    const NewFile file{.leadingComment = leadingCommentOf(parsed),
                       .headerLines =
                           textsOf(parsed, {"package_declaration"}) + textsOf(parsed, {"import_declaration"}),
                       .bodies = {textOf(parsed.bytes, extent.start, extent.end).trimmed()}};
    const QString path = siblingPath(input.path, moved.name, QStringLiteral("java"));
    Result result{moved.name, path, {}, {}};
    result.edit.changes = newFile(path, compose(file, false));
    result.edit.changes.append(DocumentEdit{input.path, {}, {deletion(parsed.bytes, extent)}});
    return result;
}

} // namespace

Result plan(const Input &input)
{
    const bool isSupported = input.languageId == QLatin1String("cpp") || input.languageId == QLatin1String("java");
    if (!isSupported)
        return {{}, {}, {}, tr("Move Class works in C++ and Java files")};
    const Parsed parsed = parse(input.languageId, input.text);
    if (!parsed.tree)
        return {{}, {}, {}, tr("The file cannot be read")};
    const QList<TopLevel> classes = topLevelClasses(input.languageId, parsed);
    const std::uint32_t cursor = offsetAt(parsed.bytes, input.cursor);
    const auto moved = std::ranges::find_if(classes, [cursor](const TopLevel &candidate) {
        return ts_node_start_byte(candidate.node) <= cursor && cursor <= ts_node_end_byte(candidate.node);
    });
    if (moved == classes.end() || moved->name.isEmpty())
        return {{}, {}, {}, tr("Place the cursor in a class")};
    if (classes.size() == 1)
        return {moved->name, {}, {}, tr("“%1” is already alone in its file").arg(moved->name)};
    if (input.languageId == QLatin1String("cpp"))
        return planCpp(input, parsed, *moved);
    return planJava(input, parsed, *moved);
}

} // namespace MoveClass
