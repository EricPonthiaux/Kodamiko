// SPDX-License-Identifier: GPL-3.0-only

#include "ChangeSignature.h"

#include "ParsedText.h"
#include "syntax/LanguageRegistry.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMap>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string_view>

using namespace LspProtocol;

namespace ChangeSignature {

namespace {

// How far above a name its parameter or argument list may be: "obj.f(…)",
// "ns::f(…)", "f<T>(…)", "def f(…)".
constexpr int maximumClimb = 6;

constexpr std::initializer_list<std::string_view> listTypes = {"argument_list", "arguments", "parameter_list",
                                                               "formal_parameters", "parameters"};

enum class ListKind {
    Arguments,
    Parameters
};

struct FoundList {
    TSNode list;
    ListKind kind = ListKind::Arguments;
};

// A list's new text, or why it cannot be written; `warning` says what is
// wrong with it, written all the same.
struct Written {
    QString text;
    QString problem;
    QString warning = {};
};

// A call's arguments: positional ones in order, Python's keyword ones by
// name.
struct Arguments {
    QStringList positional;
    QHash<QString, QString> keywords;
    QStringList keywordOrder;
};

// What a call passes to a parameter.
struct Passed {
    QString text; // empty: nothing
    bool isKeyword = false;
};

using Texts = std::map<QString, std::unique_ptr<ParsedText>>;

QString tr(const char *text)
{
    return QCoreApplication::translate("ChangeSignature", text);
}

std::string_view typeOf(TSNode node)
{
    return ts_node_type(node);
}

bool isPython(const QString &languageId)
{
    return languageId == QLatin1String("python");
}

// Whether `name` is in an import or an export ("import { f } from…",
// "from m import f", "using ns::f"): nothing to change there.
bool isImported(TSNode name)
{
    for (TSNode node = ts_node_parent(name); !ts_node_is_null(node); node = ts_node_parent(node)) {
        const std::string_view type = typeOf(node);
        const bool isImport =
            type.find("import") != std::string_view::npos || type == "export_specifier" || type == "using_declaration";
        if (isImport)
            return true;
    }
    return false;
}

// The parameter or argument list after the name `name` stands in.
std::optional<FoundList> listAround(TSNode name)
{
    TSNode node = name;
    for (int depth = 0; depth < maximumClimb; ++depth) {
        node = ts_node_parent(node);
        if (ts_node_is_null(node))
            break;
        for (const auto &[field, kind] :
             {std::pair{"arguments", ListKind::Arguments}, std::pair{"parameters", ListKind::Parameters}}) {
            const TSNode list = ts_node_child_by_field_name(node, field, uint32_t(std::strlen(field)));
            const bool isList = !ts_node_is_null(list) && std::ranges::find(listTypes, typeOf(list)) != listTypes.end();
            if (isList && ts_node_start_byte(list) >= ts_node_end_byte(name))
                return FoundList{list, kind};
        }
    }
    return std::nullopt;
}

QList<TSNode> itemsOf(TSNode list)
{
    QList<TSNode> items;
    for (uint32_t i = 0; i < ts_node_named_child_count(list); ++i) {
        const TSNode child = ts_node_named_child(list, i);
        if (typeOf(child).find("comment") == std::string_view::npos)
            items.append(child);
    }
    return items;
}

// The first identifier of `node`: a parameter's name.
QString firstIdentifier(const ParsedText &parsed, TSNode node)
{
    if (typeOf(node) == "identifier")
        return parsed.textOf(node);
    for (uint32_t i = 0; i < ts_node_named_child_count(node); ++i) {
        const QString found = firstIdentifier(parsed, ts_node_named_child(node, i));
        if (!found.isEmpty())
            return found;
    }
    return {};
}

bool isReceiver(const QString &languageId, const QString &parameter)
{
    return isPython(languageId) && (parameter == QLatin1String("self") || parameter == QLatin1String("cls"));
}

QString inner(const ParsedText &parsed, TSNode list)
{
    return parsed.textOf(ts_node_start_byte(list) + 1, ts_node_end_byte(list) - 1);
}

bool hasDefault(const QString &declaration)
{
    return declaration.contains(QLatin1Char('=')) && !declaration.contains(QLatin1String("=="));
}

// Python and C++ refuse a parameter without a default after one with a
// default: the first such parameter, empty if none.
QString misplacedRequired(const QString &languageId, const QStringList &declarations)
{
    const bool checks = isPython(languageId) || languageId == QLatin1String("cpp");
    bool defaultSeen = false;
    for (const QString &declaration : declarations) {
        const bool isVariadic = declaration.startsWith(QLatin1Char('*')) || declaration.contains(QLatin1String("..."));
        if (hasDefault(declaration))
            defaultSeen = true;
        else if (checks && defaultSeen && !isVariadic)
            return declaration;
    }
    return {};
}

Written parametersText(const QString &languageId,
                       const ParsedText &parsed,
                       const QList<TSNode> &items,
                       const Input &input)
{
    QStringList declared;
    for (const TSNode item : items)
        declared.append(parsed.textOf(item));
    QStringList written;
    if (!declared.isEmpty() && isReceiver(languageId, declared.first()))
        written.append(declared.takeFirst());
    for (const Parameter &parameter : input.parameters) {
        if (!parameter.oldIndex) {
            written.append(parameter.declaration);
            continue;
        }
        if (*parameter.oldIndex >= declared.size())
            return {{}, tr("the declaration has fewer parameters")};
        written.append(declared[*parameter.oldIndex]);
    }
    const QString misplaced = misplacedRequired(languageId, written);
    const QString warning =
        misplaced.isEmpty() ? QString() : tr("“%1” comes after a parameter with a default value").arg(misplaced);
    return {written.join(QStringLiteral(", ")), {}, warning};
}

Arguments argumentsOf(const ParsedText &parsed, const QList<TSNode> &items)
{
    Arguments arguments;
    for (const TSNode item : items) {
        if (typeOf(item) == "keyword_argument") {
            const QString name = parsed.textOf(ts_node_child_by_field_name(item, "name", 4));
            arguments.keywords.insert(name, parsed.textOf(item));
            arguments.keywordOrder.append(name);
        } else {
            arguments.positional.append(parsed.textOf(item));
        }
    }
    return arguments;
}

// The extra arguments of a variadic function, and Python's keyword
// arguments that are not parameters (**kwargs): they stay.
QStringList extraArguments(const Arguments &arguments, const QStringList &oldNames)
{
    QStringList extras = arguments.positional.mid(oldNames.size());
    for (const QString &keyword : arguments.keywordOrder) {
        if (!oldNames.contains(keyword))
            extras.append(arguments.keywords.value(keyword));
    }
    return extras;
}

// What a call passes to `parameter` (named `name`): a value, a keyword
// argument ("b=1"), or nothing (an empty text: its default).
Passed passedTo(const Parameter &parameter, const QString &name, const Arguments &arguments, bool python)
{
    if (!parameter.oldIndex)
        return {parameter.valueAtCalls, false};
    if (*parameter.oldIndex < arguments.positional.size())
        return {arguments.positional[*parameter.oldIndex], false};
    if (python && arguments.keywords.contains(name))
        return {arguments.keywords.value(name), true};
    return {};
}

Written argumentsText(const QString &languageId,
                      const ParsedText &parsed,
                      const QList<TSNode> &items,
                      const Input &input)
{
    const Arguments arguments = argumentsOf(parsed, items);
    const bool python = isPython(languageId);
    QStringList written;
    bool isPositional = true; // until an argument is left out or named
    QString leftOut;
    for (const Parameter &parameter : input.parameters) {
        const QString name = parameter.oldIndex ? input.oldNames.value(*parameter.oldIndex) : parameter.name;
        const Passed passed = passedTo(parameter, name, arguments, python);
        if (passed.isKeyword)
            written.append(passed.text);
        else if (passed.text.isEmpty())
            leftOut = name;
        if (passed.isKeyword || passed.text.isEmpty()) {
            isPositional = false;
            continue;
        }
        if (!isPositional && !python)
            return {{}, tr("the call leaves out “%1”, which now comes before an argument").arg(leftOut)};
        written.append(isPositional ? passed.text : name + QLatin1Char('=') + passed.text);
    }
    written.append(extraArguments(arguments, input.oldNames));
    return {written.join(QStringLiteral(", ")), {}};
}

QString where(const Location &place)
{
    return QStringLiteral("%1:%2").arg(QFileInfo(place.filePath).fileName()).arg(place.range.start.line + 1);
}

const ParsedText *parsedOf(const QString &path, const Input &input, Texts &texts)
{
    auto found = texts.find(path);
    if (found == texts.end()) {
        const QString languageId = LanguageRegistry::languageForFile(path);
        found = texts.emplace(path, std::make_unique<ParsedText>(languageId, input.texts.value(path))).first;
    }
    return found->second->isValid() ? found->second.get() : nullptr;
}

// The edit of the list at `place`, if it changes.
std::optional<TextEdit> editAt(const Location &place, const Input &input, Texts &texts, QStringList &conflicts)
{
    const ParsedText *parsed = parsedOf(place.filePath, input, texts);
    if (!parsed)
        return std::nullopt;
    const std::uint32_t offset = parsed->offsetAt(place.range.start);
    const TSNode name = ts_node_named_descendant_for_byte_range(parsed->root(), offset, offset);
    if (isImported(name))
        return std::nullopt;
    const std::optional<FoundList> found = listAround(name);
    if (!found) {
        conflicts.append(tr("%1: the function is used without being called there; left as it is").arg(where(place)));
        return std::nullopt;
    }
    const QString languageId = LanguageRegistry::languageForFile(place.filePath);
    const QList<TSNode> items = itemsOf(found->list);
    const Written written = found->kind == ListKind::Parameters ? parametersText(languageId, *parsed, items, input)
                                                                : argumentsText(languageId, *parsed, items, input);
    if (!written.problem.isEmpty()) {
        conflicts.append(tr("%1: %2; left as it is").arg(where(place), written.problem));
        return std::nullopt;
    }
    if (!written.warning.isEmpty())
        conflicts.append(tr("%1: %2").arg(where(place), written.warning));
    if (written.text == inner(*parsed, found->list))
        return std::nullopt;
    const Position start = parsed->positionAt(ts_node_start_byte(found->list) + 1);
    const Position end = parsed->positionAt(ts_node_end_byte(found->list) - 1);
    return TextEdit{{start, end}, written.text};
}

bool overlaps(const TextEdit &a, const TextEdit &b)
{
    return a.range.start < b.range.end && b.range.start < a.range.end;
}

} // namespace

Signature read(const QString &languageId, const QString &text, const Position &cursor)
{
    const ParsedText parsed(languageId, text);
    if (!parsed.isValid())
        return {.error = tr("Change Signature does not know this language")};
    const std::uint32_t offset = parsed.offsetAt(cursor);
    const TSNode atCursor = ts_node_named_descendant_for_byte_range(parsed.root(), offset, offset);
    const std::optional<FoundList> found = listAround(atCursor);
    if (!found || found->kind != ListKind::Parameters)
        return {.error = tr("Place the cursor on the name of a function where it is declared")};
    Signature signature{.name = parsed.textOf(atCursor)};
    for (const TSNode item : itemsOf(found->list)) {
        const QString declaration = parsed.textOf(item);
        if (signature.parameters.isEmpty() && signature.names.isEmpty() && isReceiver(languageId, declaration))
            continue;
        signature.parameters.append(declaration);
        signature.names.append(firstIdentifier(parsed, item));
    }
    return signature;
}

QString nameOf(const QString &languageId, const QString &declaration)
{
    // The declaration in a function of the language, read back.
    QString function;
    if (isPython(languageId))
        function = QStringLiteral("def f(%1):\n    pass\n");
    else if (languageId == QLatin1String("java"))
        function = QStringLiteral("class A { void f(%1) {} }");
    else if (languageId == QLatin1String("c") || languageId == QLatin1String("cpp"))
        function = QStringLiteral("void f(%1);");
    else
        function = QStringLiteral("function f(%1) {}");
    const qsizetype name = function.indexOf(QLatin1String("f("));
    const QString text = function.arg(declaration);
    const int line = int(text.left(name).count(QLatin1Char('\n')));
    const int column = int(name - (text.lastIndexOf(QLatin1Char('\n'), name - 1) + 1));
    return read(languageId, text, {line, column}).names.value(0);
}

Result plan(const Input &input)
{
    Result result;
    Texts texts;
    QMap<QString, QList<TextEdit>> editsByFile;
    for (const Location &place : input.places) {
        const std::optional<TextEdit> edit = editAt(place, input, texts, result.conflicts);
        if (!edit)
            continue;
        QList<TextEdit> &edits = editsByFile[place.filePath];
        // A call inside another's arguments: the outer one keeps it as it was.
        const bool isNested =
            std::ranges::any_of(edits, [&edit](const TextEdit &other) { return overlaps(other, *edit); });
        if (isNested) {
            result.conflicts.append(tr("%1: a call inside another's arguments; left as it is").arg(where(place)));
            continue;
        }
        edits.append(*edit);
    }
    for (auto it = editsByFile.cbegin(); it != editsByFile.cend(); ++it)
        result.edit.changes.append(DocumentEdit{it.key(), std::nullopt, it.value()});
    result.conflicts.removeDuplicates();
    return result;
}

} // namespace ChangeSignature
