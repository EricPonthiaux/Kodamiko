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
constexpr qint64 maxFileBytes = 2 * 1024 * 1024;

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

// The fields a Java class body declares: "private final List<String>
// names = new ArrayList<>(), others;" gives "names" and "others".
QList<CodeGenerationProvider::Field> fieldsIn(const QByteArray &blanked, int brace)
{
    static const QSet<QString> modifiers = {"public", "protected", "private", "static",
                                            "final",  "transient", "volatile"};
    static const QRegularExpression annotation(QStringLiteral("@[\\w.]+(?:\\s*\\([^()]*\\))?"));
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z_$][\\w$]*$"));
    QList<CodeGenerationProvider::Field> fields;
    for (const CodeScan::Declaration &declaration : CodeScan::declarationsIn(blanked, brace)) {
        if (declaration.hasBody || !CodeScan::parseFunction(declaration.text).name.isEmpty())
            continue;
        QString text = declaration.text;
        text.remove(annotation);
        const QStringList declarators = CodeScan::splitTopLevel(text);
        if (declarators.isEmpty())
            continue;
        // The modifiers and the type, before the first name.
        QStringList words = declarators.first().section(QLatin1Char('='), 0, 0).simplified().split(QLatin1Char(' '));
        bool isStatic = false;
        bool isFinal = false;
        while (!words.isEmpty() && modifiers.contains(words.first())) {
            isStatic = isStatic || words.first() == QLatin1String("static");
            isFinal = isFinal || words.first() == QLatin1String("final");
            words.removeFirst();
        }
        if (words.size() < 2 || words.first() == QLatin1String("import") || words.first() == QLatin1String("package"))
            continue;
        const QString first = words.takeLast();
        const QString type = words.join(QLatin1Char(' '));
        QStringList names{first};
        for (qsizetype i = 1; i < declarators.size(); ++i)
            names.append(declarators[i].section(QLatin1Char('='), 0, 0).trimmed());
        for (const QString &name : std::as_const(names)) {
            if (identifier.match(name).hasMatch())
                fields.append({type, name, isStatic, isFinal});
        }
    }
    return fields;
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
    int typedStart = context.prefixStart;
    if (context.prefixStart >= 2 && context.text.mid(context.prefixStart - 2, 2) == "::") {
        typedStart = context.prefixStart - 2;
        while (typedStart > 0 && CodeScan::isIdentifierByte(context.text[typedStart - 1]))
            --typedStart;
    }
    const int start = declarationStart(context.text, typedStart);
    if (start < 0) {
        done(items, false);
        return;
    }
    const int brace = CodeScan::enclosingBrace(blanked, start);
    const CodeScan::ClassHeader header = CodeScan::classAt(blanked, brace);
    const bool inClass = !header.name.isEmpty() && header.keyword != QLatin1String("enum");
    if (inClass && typedStart == context.prefixStart) {
        addOverrides(context, brace, items);
    } else if (!inClass && context.languageId != QLatin1String("java")) {
        // At the top level, or in a namespace.
        const int blockStart = brace < 0 ? -1 : CodeScan::headerStart(blanked, brace);
        const QByteArray block = brace < 0 ? QByteArray() : blanked.mid(blockStart, brace - blockStart).trimmed();
        if (brace < 0 || block.startsWith("namespace") || block.startsWith("extern"))
            addImplementations(context, items);
    }
    for (CompletionItem &item : items)
        item.replaceStart = start;
    if (typedStart != context.prefixStart) {
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
    // What the class declares already.
    QSet<QString> declared;
    for (const CodeScan::Declaration &declaration : CodeScan::declarationsIn(blanked, brace)) {
        const CodeScan::Function function = CodeScan::parseFunction(declaration.text);
        if (!function.name.isEmpty())
            declared.insert(signatureKey(function));
    }
    const QString indent = CodeScan::lineIndent(context.text, context.prefixStart);
    const QString unit = CodeScan::indentUnit(context.text);
    const bool definition = java || !isHeader(context.filePath);
    const QString title = QObject::tr("Override");

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
        const bool interface = baseHeader.keyword == QLatin1String("interface");
        for (const CodeScan::Declaration &declaration : CodeScan::declarationsIn(source.blanked, source.body)) {
            const CodeScan::Function function = CodeScan::parseFunction(declaration.text);
            if (function.name.isEmpty() || function.name == base || function.name.startsWith(QLatin1Char('~')) ||
                function.deleted || function.returnType.isEmpty())
                continue;
            const QStringList &specifiers = function.specifiers;
            if (java) {
                if (specifiers.contains(QStringLiteral("static")) || specifiers.contains(QStringLiteral("private")) ||
                    specifiers.contains(QStringLiteral("final")))
                    continue;
            } else {
                const bool overridable = specifiers.contains(QStringLiteral("virtual")) ||
                                         function.trailing.contains(QLatin1String("override"));
                if (!overridable || function.trailing.contains(QLatin1String("final")))
                    continue;
            }
            const QString key = signatureKey(function);
            if (declared.contains(key))
                continue;
            declared.insert(key); // once, from the nearest base
            const bool pure = function.pure || interface;
            const QString call = (function.returnsValue() ? QStringLiteral("return ") : QString()) +
                                 (java ? QStringLiteral("super.") : base + QStringLiteral("::")) + function.name +
                                 QLatin1Char('(') + argumentsOf(function, true) + QStringLiteral(");");
            const QString body =
                pure ? QStringLiteral("\t$0") : QStringLiteral("\t") + Snippet::escape(call) + QStringLiteral("$0");
            QString code;
            if (java) {
                const QString visibility =
                    interface || specifiers.contains(QStringLiteral("public")) ? QStringLiteral("public ")
                    : specifiers.contains(QStringLiteral("protected"))         ? QStringLiteral("protected ")
                                                                               : QString();
                const QString qualifiers = function.qualifiers();
                code = QStringLiteral("@Override\n") + visibility +
                       Snippet::escape(function.returnType + QLatin1Char(' ') + function.name + QLatin1Char('(') +
                                       parametersOf(function) + QLatin1Char(')') +
                                       (qualifiers.isEmpty() ? QString() : QLatin1Char(' ') + qualifiers)) +
                       QStringLiteral(" {\n") + body + QStringLiteral("\n}");
            } else {
                const QString qualifiers = function.qualifiers();
                const QString signature = function.returnType + QLatin1Char(' ') + function.name + QLatin1Char('(') +
                                          parametersOf(function) + QLatin1Char(')') +
                                          (qualifiers.isEmpty() ? QString() : QLatin1Char(' ') + qualifiers) +
                                          QStringLiteral(" override");
                code = definition ? Snippet::escape(signature) + QStringLiteral("\n{\n") + body + QStringLiteral("\n}")
                                  : Snippet::escape(signature) + QStringLiteral(";$0");
            }
            CompletionItem item = itemOf(function.name, QObject::tr("override %1").arg(base),
                                         CodeScan::indented(code, indent, unit), title);
            item.type = function.returnType;
            items.append(item);
        }
    }
    if (java && own.keyword == QLatin1String("class"))
        addAccessors(blanked, brace, own.name, declared, indent, unit, items);
    if (java && own.keyword == QLatin1String("class")) {
        // Object's.
        static const QList<std::pair<const char *, const char *>> object = {
            {"toString", "@Override\npublic String toString() {\n\treturn super.toString();$0\n}"},
            {"equals", "@Override\npublic boolean equals(Object other) {\n\treturn super.equals(other);$0\n}"},
            {"hashCode", "@Override\npublic int hashCode() {\n\treturn super.hashCode();$0\n}"},
        };
        for (const auto &[name, code] : object) {
            const QString key =
                QString::fromLatin1(name) +
                (QLatin1String(name) == QLatin1String("equals") ? QStringLiteral("/1") : QStringLiteral("/0"));
            if (declared.contains(key))
                continue;
            items.append(itemOf(QString::fromLatin1(name), QObject::tr("override Object"),
                                CodeScan::indented(QString::fromLatin1(code), indent, unit), title));
        }
    }
}

void CodeGenerationProvider::addAccessors(const QByteArray &blanked,
                                          int brace,
                                          const QString &className,
                                          const QSet<QString> &declared,
                                          const QString &indent,
                                          const QString &unit,
                                          QList<CompletionItem> &items)
{
    const QString getterTitle = QObject::tr("Getter");
    const QString setterTitle = QObject::tr("Setter");
    for (const Field &field : fieldsIn(blanked, brace)) {
        // "isEnabled" for a boolean, as IntelliJ names them.
        const QString capitalized = field.name.left(1).toUpper() + field.name.mid(1);
        const QString getter =
            (field.type == QLatin1String("boolean") ? QStringLiteral("is") : QStringLiteral("get")) + capitalized;
        const QString modifiers = field.isStatic ? QStringLiteral("public static ") : QStringLiteral("public ");
        if (!declared.contains(getter + QStringLiteral("/0"))) {
            const QString code =
                Snippet::escape(modifiers + field.type + QLatin1Char(' ') + getter + QStringLiteral("() {\n\treturn ") +
                                field.name + QStringLiteral(";\n}")) +
                QStringLiteral("$0");
            CompletionItem item = itemOf(getter, QObject::tr("getter · %1 %2").arg(field.type, field.name),
                                         CodeScan::indented(code, indent, unit), getterTitle);
            item.type = field.type;
            items.append(item);
        }
        const QString setter = QStringLiteral("set") + capitalized;
        if (field.isFinal || declared.contains(setter + QStringLiteral("/1")))
            continue;
        const QString target = (field.isStatic ? className : QStringLiteral("this")) + QLatin1Char('.') + field.name;
        const QString code = Snippet::escape(modifiers + QStringLiteral("void ") + setter + QLatin1Char('(') +
                                             field.type + QLatin1Char(' ') + field.name + QStringLiteral(") {\n\t") +
                                             target + QStringLiteral(" = ") + field.name + QStringLiteral(";\n}")) +
                             QStringLiteral("$0");
        items.append(itemOf(setter, QObject::tr("setter · %1 %2").arg(field.type, field.name),
                            CodeScan::indented(code, indent, unit), setterTitle));
    }
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

    // What the source defines already: "Foo::size/0".
    QSet<QString> defined;
    const QByteArray blanked = CodeScan::blank(context.text);
    std::function<void(int)> collect = [&](int open) {
        for (const CodeScan::Declaration &declaration : CodeScan::declarationsIn(blanked, open)) {
            if (!declaration.hasBody)
                continue;
            if (declaration.text.startsWith(QLatin1String("namespace"))) {
                collect(declaration.body);
                continue;
            }
            const CodeScan::Function function = CodeScan::parseFunction(declaration.text);
            if (!function.name.isEmpty())
                defined.insert(function.scope + QStringLiteral("::") + signatureKey(function));
        }
    };
    collect(-1);

    const QString title = QObject::tr("Implement");
    const QString file = QFileInfo(headerPath).fileName();
    const QString indent = CodeScan::lineIndent(context.text, context.prefixStart);
    const QString unit = CodeScan::indentUnit(context.text);
    // The header's declarations without a body, in classes and
    // namespaces.
    std::function<void(int, const QString &)> walk = [&](int open, const QString &scope) {
        for (const CodeScan::Declaration &declaration : CodeScan::declarationsIn(header, open)) {
            if (declaration.text.startsWith(QLatin1String("template")) ||
                declaration.text.startsWith(QLatin1String("friend")) ||
                declaration.access.contains(QLatin1String("signals")) ||
                declaration.access.contains(QLatin1String("SIGNALS")))
                continue;
            if (declaration.hasBody) {
                const CodeScan::ClassHeader nested = CodeScan::classAt(header, declaration.body);
                if (!nested.name.isEmpty() && nested.keyword != QLatin1String("enum"))
                    walk(declaration.body, scope.isEmpty() ? nested.name : scope + QStringLiteral("::") + nested.name);
                else if (declaration.text.startsWith(QLatin1String("namespace")))
                    walk(declaration.body, scope);
                continue;
            }
            const CodeScan::Function function = CodeScan::parseFunction(declaration.text);
            if (function.name.isEmpty() || function.pure || function.deleted ||
                !function.templateParameters.isEmpty() || function.specifiers.contains(QStringLiteral("inline")) ||
                function.specifiers.contains(QStringLiteral("constexpr")) ||
                function.specifiers.contains(QStringLiteral("friend")))
                continue;
            if (defined.contains(scope + QStringLiteral("::") + signatureKey(function)))
                continue;
            const QString qualified = scope.isEmpty() ? function.name : scope + QStringLiteral("::") + function.name;
            const QString qualifiers = function.qualifiers();
            const QString signature =
                (function.returnType.isEmpty() ? QString() : function.returnType + QLatin1Char(' ')) + qualified +
                QLatin1Char('(') + parametersOf(function) + QLatin1Char(')') +
                (qualifiers.isEmpty() ? QString() : QLatin1Char(' ') + qualifiers);
            const QString code = Snippet::escape(signature) + QStringLiteral("\n{\n\t$0\n}");
            // Matched by its name ("~" left out), the class in the detail.
            QString name = function.name;
            if (name.startsWith(QLatin1Char('~')))
                name = name.mid(1);
            CompletionItem item = itemOf(name, QObject::tr("%1 · %2").arg(qualified, file),
                                         CodeScan::indented(code, indent, unit), title);
            item.type = function.returnType;
            items.append(item);
        }
    };
    walk(-1, QString());
}
