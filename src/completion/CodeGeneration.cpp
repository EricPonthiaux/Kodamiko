// SPDX-License-Identifier: GPL-3.0-only

#include "CodeGeneration.h"

#include "CodeScan.h"
#include "Snippet.h"
#include "navigation/SymbolIndex.h"

#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QSet>

namespace {

// How many base classes are followed up the hierarchy.
constexpr int maxBaseClasses = 12;
// Bigger files are no header worth reading at every keystroke.
constexpr qint64 maxFileBytes = qint64{2} * 1024 * 1024;

bool isHeader(const QString &path)
{
    static const QSet<QString> suffixes = {"h", "hh", "hpp", "hxx", "h++", "inl"};
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

bool isSource(const QString &path)
{
    static const QSet<QString> suffixes = {"c", "cc", "cpp", "cxx", "c++", "m", "mm"};
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

// The names of the parameters, for a call that passes them on.
QString argumentsOf(const CodeScan::Function &function, bool typed)
{
    QStringList names;
    for (const CodeScan::Parameter &parameter : function.parameterList(typed))
        names.append(parameter.name);
    return names.join(QStringLiteral(", "));
}

// The parameters as a definition repeats them: without default values.
QString parametersOf(const CodeScan::Function &function)
{
    QStringList parameters;
    for (const CodeScan::Parameter &parameter : function.parameterList())
        parameters.append(parameter.text);
    return parameters.join(QStringLiteral(", "));
}

QString signatureKey(const CodeScan::Function &function)
{
    return function.name + QLatin1Char('/') + QString::number(function.parameterList().size());
}

// Where what is typed starts: before "virtual", "public"... typed
// already, which the generated code repeats. -1 when no member or
// function can be declared there.
int declarationStart(const QByteArray &text, int at)
{
    static const QSet<QByteArray> words = {"virtual", "public", "protected", "private", "inline"};
    int start = at;
    while (true) {
        if (CodeScan::startsStatement(text, start, true))
            return start;
        int end = start;
        while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t'))
            --end;
        int wordStart = end;
        while (wordStart > 0 && CodeScan::isIdentifierByte(text[wordStart - 1]))
            --wordStart;
        if (wordStart == end || !words.contains(text.mid(wordStart, end - wordStart)))
            return -1;
        start = wordStart;
    }
}

CompletionItem itemOf(const QString &label, const QString &detail, const QString &code, const QString &title)
{
    CompletionItem item;
    item.label = label;
    item.detail = detail;
    item.kind = CompletionItem::Kind::Method;
    item.insertText = code;
    item.isSnippet = true;
    item.priority = 8;
    item.documentation = title + QStringLiteral("\n\n") + Snippet::expand(code).text;
    return item;
}

// "Foo::size" in `scope`, "size" at the top level.
QString qualifiedName(const QString &scope, const QString &name)
{
    return scope.isEmpty() ? name : scope + QStringLiteral("::") + name;
}

// " const noexcept" after the parameters, "" without qualifiers.
QString qualifiersSuffix(const CodeScan::Function &function)
{
    const QString qualifiers = function.qualifiers();
    return qualifiers.isEmpty() ? QString() : QLatin1Char(' ') + qualifiers;
}

// "int size(int a) const": the return type, the name, the parameters
// without default values and the qualifiers.
QString signatureOf(const CodeScan::Function &function, const QString &name)
{
    const QString returnType = function.returnType.isEmpty() ? QString() : function.returnType + QLatin1Char(' ');
    return returnType + name + QLatin1Char('(') + parametersOf(function) + QLatin1Char(')') +
           qualifiersSuffix(function);
}

// The modifiers and the type of a Java field declaration, before its
// first name: "private static final int" gives "int", static and final.
struct FieldType {
    QStringList words; // the type, then the first name
    bool isStatic = false;
    bool isFinal = false;
};

FieldType fieldTypeOf(const QString &firstDeclarator)
{
    static const QSet<QString> modifiers = {"public", "protected", "private", "static",
                                            "final",  "transient", "volatile"};
    FieldType fieldType;
    fieldType.words = firstDeclarator.section(QLatin1Char('='), 0, 0).simplified().split(QLatin1Char(' '));
    QStringList &words = fieldType.words;
    while (!words.isEmpty() && modifiers.contains(words.first())) {
        fieldType.isStatic = fieldType.isStatic || words.first() == QLatin1String("static");
        fieldType.isFinal = fieldType.isFinal || words.first() == QLatin1String("final");
        words.removeFirst();
    }
    return fieldType;
}

// The fields one Java declaration declares: "private final List<String>
// names = new ArrayList<>(), others;" gives "names" and "others".
QList<CodeGenerationProvider::Field> fieldsOf(const QString &declarationText)
{
    static const QRegularExpression annotation(QStringLiteral("@[\\w.]+(?:\\s*\\([^()]*\\))?"));
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z_$][\\w$]*$"));
    QString text = declarationText;
    text.remove(annotation);
    const QStringList declarators = CodeScan::splitTopLevel(text);
    if (declarators.isEmpty())
        return {};
    FieldType fieldType = fieldTypeOf(declarators.first());
    QStringList &words = fieldType.words;
    const bool isImportOrPackage =
        !words.isEmpty() && (words.first() == QLatin1String("import") || words.first() == QLatin1String("package"));
    if (words.size() < 2 || isImportOrPackage)
        return {};
    QStringList names{words.takeLast()};
    const QString type = words.join(QLatin1Char(' '));
    for (qsizetype i = 1; i < declarators.size(); ++i)
        names.append(declarators[i].section(QLatin1Char('='), 0, 0).trimmed());
    QList<CodeGenerationProvider::Field> fields;
    for (const QString &name : std::as_const(names)) {
        if (identifier.match(name).hasMatch())
            fields.append({type, name, fieldType.isStatic, fieldType.isFinal});
    }
    return fields;
}

// The fields a Java class body declares.
QList<CodeGenerationProvider::Field> fieldsIn(const QByteArray &blanked, int brace)
{
    QList<CodeGenerationProvider::Field> fields;
    for (const CodeScan::Declaration &declaration : CodeScan::declarationsIn(blanked, brace)) {
        const bool isFunction = !CodeScan::parseFunction(declaration.text).name.isEmpty();
        if (!declaration.hasBody && !isFunction)
            fields.append(fieldsOf(declaration.text));
    }
    return fields;
}

// Where generated code goes: the indentation of the line typed in, and
// the file's indentation step.
struct Layout {
    QString indent;
    QString unit;
};

// "Foo::" typed before the cursor: where "Foo" starts, else
// `context.prefixStart`.
int typedScopeStart(const CompletionContext &context)
{
    const int prefixStart = context.prefixStart;
    if (prefixStart < 2 || context.text.mid(prefixStart - 2, 2) != "::")
        return prefixStart;
    int start = prefixStart - 2;
    while (start > 0 && CodeScan::isIdentifierByte(context.text[start - 1]))
        --start;
    return start;
}

// Whether the block opened at `brace` (-1: none) is the top level, a
// namespace or an extern "C" block: where functions are defined.
bool isDefinitionScope(const QByteArray &blanked, int brace)
{
    if (brace < 0)
        return true;
    const int blockStart = CodeScan::headerStart(blanked, brace);
    const QByteArray block = blanked.mid(blockStart, brace - blockStart).trimmed();
    return block.startsWith("namespace") || block.startsWith("extern");
}

// The signatures ("size/0") of the functions declared in the class body
// opened at `brace`.
QSet<QString> declaredSignatures(const QByteArray &blanked, int brace)
{
    QSet<QString> declared;
    for (const CodeScan::Declaration &declaration : CodeScan::declarationsIn(blanked, brace)) {
        const CodeScan::Function function = CodeScan::parseFunction(declaration.text);
        if (!function.name.isEmpty())
            declared.insert(signatureKey(function));
    }
    return declared;
}

// What every generated override shares.
struct OverrideStyle {
    bool java = false;
    bool definition = false; // with a body, not only a declaration
    Layout layout;
    QString title;
};

// A base class whose functions may be overridden.
struct BaseClass {
    QString name;
    bool isInterface = false;
    QList<CodeScan::Declaration> declarations;
};

bool canOverride(const CodeScan::Function &function, const QString &base, bool java)
{
    const bool isSpecial = function.name.isEmpty() || function.name == base ||
                           function.name.startsWith(QLatin1Char('~')) || function.returnType.isEmpty();
    if (isSpecial || function.deleted)
        return false;
    const QStringList &specifiers = function.specifiers;
    if (java) {
        const bool isClosed = specifiers.contains(QStringLiteral("static")) ||
                              specifiers.contains(QStringLiteral("private")) ||
                              specifiers.contains(QStringLiteral("final"));
        return !isClosed;
    }
    const bool overridable =
        specifiers.contains(QStringLiteral("virtual")) || function.trailing.contains(QLatin1String("override"));
    return overridable && !function.trailing.contains(QLatin1String("final"));
}

QString javaVisibility(const CodeScan::Function &function, bool isInterface)
{
    if (isInterface || function.specifiers.contains(QStringLiteral("public")))
        return QStringLiteral("public ");
    if (function.specifiers.contains(QStringLiteral("protected")))
        return QStringLiteral("protected ");
    return {};
}

// The body of an override: empty for a pure function, else a call to
// the base's.
QString overrideBody(const CodeScan::Function &function, const BaseClass &base, bool java)
{
    if (function.pure || base.isInterface)
        return QStringLiteral("\t$0");
    const QString call = (function.returnsValue() ? QStringLiteral("return ") : QString()) +
                         (java ? QStringLiteral("super.") : base.name + QStringLiteral("::")) + function.name +
                         QLatin1Char('(') + argumentsOf(function, true) + QStringLiteral(");");
    return QStringLiteral("\t") + Snippet::escape(call) + QStringLiteral("$0");
}

QString overrideCode(const CodeScan::Function &function, const BaseClass &base, const OverrideStyle &style)
{
    const QString body = overrideBody(function, base, style.java);
    if (style.java) {
        return QStringLiteral("@Override\n") + javaVisibility(function, base.isInterface) +
               Snippet::escape(signatureOf(function, function.name)) + QStringLiteral(" {\n") + body +
               QStringLiteral("\n}");
    }
    const QString signature = signatureOf(function, function.name) + QStringLiteral(" override");
    if (style.definition)
        return Snippet::escape(signature) + QStringLiteral("\n{\n") + body + QStringLiteral("\n}");
    return Snippet::escape(signature) + QStringLiteral(";$0");
}

// The overrides `base` offers. Their signatures are added to `declared`:
// a function is offered once, from the nearest base.
QList<CompletionItem> overridesFrom(const BaseClass &base, const OverrideStyle &style, QSet<QString> &declared)
{
    QList<CompletionItem> items;
    for (const CodeScan::Declaration &declaration : base.declarations) {
        const CodeScan::Function function = CodeScan::parseFunction(declaration.text);
        if (!canOverride(function, base.name, style.java))
            continue;
        const QString key = signatureKey(function);
        if (declared.contains(key))
            continue;
        declared.insert(key);
        const QString code = overrideCode(function, base, style);
        CompletionItem item = itemOf(function.name, QObject::tr("override %1").arg(base.name),
                                     CodeScan::indented(code, style.layout.indent, style.layout.unit), style.title);
        item.type = function.returnType;
        items.append(item);
    }
    return items;
}

// Object's methods every Java class may override.
QList<CompletionItem> objectOverrides(const QSet<QString> &declared, const OverrideStyle &style)
{
    static const QList<std::pair<const char *, const char *>> object = {
        {"toString", "@Override\npublic String toString() {\n\treturn super.toString();$0\n}"},
        {"equals", "@Override\npublic boolean equals(Object other) {\n\treturn super.equals(other);$0\n}"},
        {"hashCode", "@Override\npublic int hashCode() {\n\treturn super.hashCode();$0\n}"},
    };
    QList<CompletionItem> items;
    for (const auto &[name, code] : object) {
        const bool takesOne = QLatin1String(name) == QLatin1String("equals");
        const QString key = QString::fromLatin1(name) + (takesOne ? QStringLiteral("/1") : QStringLiteral("/0"));
        if (declared.contains(key))
            continue;
        items.append(itemOf(QString::fromLatin1(name), QObject::tr("override Object"),
                            CodeScan::indented(QString::fromLatin1(code), style.layout.indent, style.layout.unit),
                            style.title));
    }
    return items;
}

// "isEnabled" for a boolean, as IntelliJ names them, else "getName".
QString getterName(const CodeGenerationProvider::Field &field, const QString &capitalized)
{
    return (field.type == QLatin1String("boolean") ? QStringLiteral("is") : QStringLiteral("get")) + capitalized;
}

QString accessorModifiers(const CodeGenerationProvider::Field &field)
{
    return field.isStatic ? QStringLiteral("public static ") : QStringLiteral("public ");
}

CompletionItem getterItem(const CodeGenerationProvider::Field &field, const QString &getter, const Layout &layout)
{
    const QString code = Snippet::escape(accessorModifiers(field) + field.type + QLatin1Char(' ') + getter +
                                         QStringLiteral("() {\n\treturn ") + field.name + QStringLiteral(";\n}")) +
                         QStringLiteral("$0");
    CompletionItem item = itemOf(getter, QObject::tr("getter · %1 %2").arg(field.type, field.name),
                                 CodeScan::indented(code, layout.indent, layout.unit), QObject::tr("Getter"));
    item.type = field.type;
    return item;
}

CompletionItem setterItem(const CodeGenerationProvider::Field &field,
                          const QString &setter,
                          const QString &className,
                          const Layout &layout)
{
    const QString target = (field.isStatic ? className : QStringLiteral("this")) + QLatin1Char('.') + field.name;
    const QString code =
        Snippet::escape(accessorModifiers(field) + QStringLiteral("void ") + setter + QLatin1Char('(') + field.type +
                        QLatin1Char(' ') + field.name + QStringLiteral(") {\n\t") + target + QStringLiteral(" = ") +
                        field.name + QStringLiteral(";\n}")) +
        QStringLiteral("$0");
    return itemOf(setter, QObject::tr("setter · %1 %2").arg(field.type, field.name),
                  CodeScan::indented(code, layout.indent, layout.unit), QObject::tr("Setter"));
}

// The getters and setters of `fields` that `declared` still lacks.
QList<CompletionItem> accessorsOf(const QList<CodeGenerationProvider::Field> &fields,
                                  const QString &className,
                                  const QSet<QString> &declared,
                                  const Layout &layout)
{
    QList<CompletionItem> items;
    for (const CodeGenerationProvider::Field &field : fields) {
        const QString capitalized = field.name.left(1).toUpper() + field.name.mid(1);
        const QString getter = getterName(field, capitalized);
        if (!declared.contains(getter + QStringLiteral("/0")))
            items.append(getterItem(field, getter, layout));
        const QString setter = QStringLiteral("set") + capitalized;
        if (!field.isFinal && !declared.contains(setter + QStringLiteral("/1")))
            items.append(setterItem(field, setter, className, layout));
    }
    return items;
}

// What a source file defines already: "Foo::size/0", in namespaces too.
QSet<QString> definedSignatures(const QByteArray &blanked, int open)
{
    QSet<QString> defined;
    for (const CodeScan::Declaration &declaration : CodeScan::declarationsIn(blanked, open)) {
        if (!declaration.hasBody)
            continue;
        if (declaration.text.startsWith(QLatin1String("namespace"))) {
            defined.unite(definedSignatures(blanked, declaration.body));
            continue;
        }
        const CodeScan::Function function = CodeScan::parseFunction(declaration.text);
        if (!function.name.isEmpty())
            defined.insert(function.scope + QStringLiteral("::") + signatureKey(function));
    }
    return defined;
}

// A header and what its source defines already.
struct ImplementationSource {
    QByteArray header; // blanked
    QString file;      // the header's name, shown in the detail
    QSet<QString> defined;
    Layout layout;
    QString title;
};

// Templates, friends and Qt signals are not defined in a source file.
bool isSkippedInHeader(const CodeScan::Declaration &declaration)
{
    return declaration.text.startsWith(QLatin1String("template")) ||
           declaration.text.startsWith(QLatin1String("friend")) ||
           declaration.access.contains(QLatin1String("signals")) ||
           declaration.access.contains(QLatin1String("SIGNALS"));
}

bool needsDefinition(const CodeScan::Function &function)
{
    const QStringList &specifiers = function.specifiers;
    const bool isDefinedInline = specifiers.contains(QStringLiteral("inline")) ||
                                 specifiers.contains(QStringLiteral("constexpr")) ||
                                 specifiers.contains(QStringLiteral("friend"));
    const bool hasNoBody = function.pure || function.deleted;
    return !function.name.isEmpty() && !hasNoBody && function.templateParameters.isEmpty() && !isDefinedInline;
}

CompletionItem implementationItem(const CodeScan::Function &function,
                                  const QString &scope,
                                  const ImplementationSource &source)
{
    const QString qualified = qualifiedName(scope, function.name);
    const QString code = Snippet::escape(signatureOf(function, qualified)) + QStringLiteral("\n{\n\t$0\n}");
    // Matched by its name ("~" left out), the class in the detail.
    QString name = function.name;
    if (name.startsWith(QLatin1Char('~')))
        name = name.mid(1);
    CompletionItem item = itemOf(name, QObject::tr("%1 · %2").arg(qualified, source.file),
                                 CodeScan::indented(code, source.layout.indent, source.layout.unit), source.title);
    item.type = function.returnType;
    return item;
}

// The header's declarations without a body still without a definition,
// in the block opened at `open` (-1: the whole header), its classes and
// its namespaces.
QList<CompletionItem> implementationsIn(const ImplementationSource &source, int open, const QString &scope)
{
    QList<CompletionItem> items;
    for (const CodeScan::Declaration &declaration : CodeScan::declarationsIn(source.header, open)) {
        if (isSkippedInHeader(declaration))
            continue;
        if (declaration.hasBody) {
            const CodeScan::ClassHeader nested = CodeScan::classAt(source.header, declaration.body);
            if (!nested.name.isEmpty() && nested.keyword != QLatin1String("enum"))
                items.append(implementationsIn(source, declaration.body, qualifiedName(scope, nested.name)));
            else if (declaration.text.startsWith(QLatin1String("namespace")))
                items.append(implementationsIn(source, declaration.body, scope));
            continue;
        }
        const CodeScan::Function function = CodeScan::parseFunction(declaration.text);
        const bool isDefined = source.defined.contains(scope + QStringLiteral("::") + signatureKey(function));
        if (needsDefinition(function) && !isDefined)
            items.append(implementationItem(function, scope, source));
    }
    return items;
}

} // namespace

CodeGenerationProvider::CodeGenerationProvider(const SymbolIndex *index, Counterpart counterpart)
    : index_(index)
    , counterpart_(std::move(counterpart))
{
}

bool CodeGenerationProvider::handles(const CompletionContext &context) const
{
    const QString &language = context.languageId;
    if (language != QLatin1String("cpp") && language != QLatin1String("c") && language != QLatin1String("java"))
        return false;
    // "Foo::" in C++ names the class whose function to implement.
    return context.triggerCharacter.isNull() || (language == QLatin1String("cpp") && context.triggerCharacter == u':');
}

QString CodeGenerationProvider::triggerCharacters(const QString &languageId) const
{
    return languageId == QLatin1String("cpp") ? QStringLiteral(":") : QString();
}

QByteArray CodeGenerationProvider::readFile(const QString &path) const
{
    QFile file(path);
    if (file.size() > maxFileBytes || !file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

CodeGenerationProvider::ClassSource CodeGenerationProvider::findClass(const QString &name,
                                                                      const CompletionContext &context) const
{
    ClassSource source;
    source.java = context.languageId == QLatin1String("java");
    source.text = context.text;
    source.blanked = CodeScan::blank(context.text);
    source.body = CodeScan::classBody(source.blanked, name);
    if (source.body >= 0 || !index_)
        return source;
    for (const SymbolLocation &symbol : index_->lookup(name)) {
        if (symbol.kind != SymbolKind::Class && symbol.kind != SymbolKind::Struct &&
            symbol.kind != SymbolKind::Interface)
            continue;
        if (symbol.filePath == context.filePath)
            continue; // read above, unsaved changes included
        source.text = readFile(symbol.filePath);
        source.blanked = CodeScan::blank(source.text);
        source.body = CodeScan::classBody(source.blanked, name);
        if (source.body >= 0)
            return source;
    }
    source.body = -1;
    return source;
}

void CodeGenerationProvider::complete(const CompletionContext &context, const Callback &done)
{
    QList<CompletionItem> items;
    const QByteArray blanked = CodeScan::blank(context.text);
    // "Foo::|": the class named before.
    const int typedStart = typedScopeStart(context);
    const int start = declarationStart(context.text, typedStart);
    if (start < 0) {
        done(items, false);
        return;
    }
    const int brace = CodeScan::enclosingBrace(blanked, start);
    const CodeScan::ClassHeader header = CodeScan::classAt(blanked, brace);
    const bool inClass = !header.name.isEmpty() && header.keyword != QLatin1String("enum");
    const bool typedScope = typedStart != context.prefixStart;
    if (inClass && !typedScope)
        addOverrides(context, brace, items);
    else if (!inClass && context.languageId != QLatin1String("java") && isDefinitionScope(blanked, brace))
        addImplementations(context, items);
    for (CompletionItem &item : items)
        item.replaceStart = start;
    if (typedScope) {
        // Only the functions of the class typed.
        const QString scope = QString::fromUtf8(context.text.mid(typedStart, context.prefixStart - 2 - typedStart));
        items.removeIf(
            [&scope](const CompletionItem &item) { return !item.detail.startsWith(scope + QStringLiteral("::")); });
    }
    done(items, false);
}

void CodeGenerationProvider::addOverrides(const CompletionContext &context,
                                          int brace,
                                          QList<CompletionItem> &items) const
{
    const bool java = context.languageId == QLatin1String("java");
    const QByteArray blanked = CodeScan::blank(context.text);
    const CodeScan::ClassHeader own = CodeScan::classAt(blanked, brace);
    QSet<QString> declared = declaredSignatures(blanked, brace);
    const OverrideStyle style{
        .java = java,
        .definition = java || !isHeader(context.filePath),
        .layout = {CodeScan::lineIndent(context.text, context.prefixStart), CodeScan::indentUnit(context.text)},
        .title = QObject::tr("Override"),
    };

    QStringList queue = own.bases;
    QSet<QString> visited{own.name};
    int visitedCount = 0;
    while (!queue.isEmpty() && visitedCount < maxBaseClasses) {
        const QString base = queue.takeFirst();
        if (visited.contains(base))
            continue;
        visited.insert(base);
        ++visitedCount;
        const ClassSource source = findClass(base, context);
        if (source.body < 0)
            continue;
        const CodeScan::ClassHeader baseHeader = CodeScan::classAt(source.blanked, source.body);
        queue.append(baseHeader.bases);
        const BaseClass baseClass{base, baseHeader.keyword == QLatin1String("interface"),
                                  CodeScan::declarationsIn(source.blanked, source.body)};
        items.append(overridesFrom(baseClass, style, declared));
    }
    if (!java || own.keyword != QLatin1String("class"))
        return;
    items.append(accessorsOf(fieldsIn(blanked, brace), own.name, declared, style.layout));
    items.append(objectOverrides(declared, style));
}

void CodeGenerationProvider::addImplementations(const CompletionContext &context, QList<CompletionItem> &items) const
{
    if (!counterpart_ || !isSource(context.filePath))
        return;
    const QString headerPath = counterpart_(context.filePath);
    if (headerPath.isEmpty() || headerPath == context.filePath)
        return;
    const QByteArray header = CodeScan::blank(readFile(headerPath));
    if (header.isEmpty())
        return;
    const ImplementationSource source{
        .header = header,
        .file = QFileInfo(headerPath).fileName(),
        .defined = definedSignatures(CodeScan::blank(context.text), -1),
        .layout = {CodeScan::lineIndent(context.text, context.prefixStart), CodeScan::indentUnit(context.text)},
        .title = QObject::tr("Implement"),
    };
    items.append(implementationsIn(source, -1, QString()));
}
