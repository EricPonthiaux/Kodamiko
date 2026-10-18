#pragma once

#include "Completion.h"

#include <QHash>
#include <QSet>

#include <functional>

class SymbolIndex;

// Code written from declarations elsewhere, as CLion's Generate menu does,
// offered in the completion list. For C++ and Java.
//
// - In a class body, the methods of its base classes that can be
//   overridden: typing "pai" offers "paintEvent", which inserts
//   "void paintEvent(QPaintEvent *event) override;" (with a body in a
//   source file; "@Override public ..." with a body in Java).
// - At the top level of a C++ source file, the functions of its header
//   still without a definition: "size" (or "Foo::si") inserts
//   "int Foo::size() const\n{\n}".
// - In a Java class body, the getters and setters of its fields still
//   without one: "getN" offers "getName", which inserts
//   "public String getName() {\n    return name;\n}".
//
// Base classes are looked for in the current file, then through the
// project's index; implementations in the header next to the source
// (`counterpart`).
class CodeGenerationProvider : public ICompletionProvider {
public:
    using Counterpart = std::function<QString(const QString &filePath)>;
    // A field of a Java class.
    struct Field {
        QString type;
        QString name;
        bool isStatic = false;
        bool isFinal = false;
    };

    CodeGenerationProvider(const SymbolIndex *index, Counterpart counterpart);

    bool handles(const CompletionContext &context) const override;
    void complete(const CompletionContext &context, const Callback &done) override;
    // "::" in C++: "Foo::" lists what Foo has to implement, without a
    // language server too.
    QString triggerCharacters(const QString &languageId) const override;

private:
    struct ClassSource {
        QByteArray text;
        QByteArray blanked;
        int body = -1;
        bool java = false;
    };
    // The body of the class `name`: in `context`'s text, else in the file
    // the index has it in.
    ClassSource findClass(const QString &name, const CompletionContext &context) const;
    void addOverrides(const CompletionContext &context, int brace, QList<CompletionItem> &items) const;
    void addImplementations(const CompletionContext &context, QList<CompletionItem> &items) const;
    static void addAccessors(const QByteArray &blanked, int brace, const QString &className,
                             const QSet<QString> &declared, const QString &indent, const QString &unit,
                             QList<CompletionItem> &items);
    QByteArray readFile(const QString &path) const;

    const SymbolIndex *index_;
    Counterpart counterpart_;
};
