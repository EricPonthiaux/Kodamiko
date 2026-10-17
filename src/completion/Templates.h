#pragma once

#include "Completion.h"

// Templates in the completion list, as in CLion and IntelliJ.

// Postfix templates: typed after an expression and a dot, they wrap it.
// "items.for" becomes "for (auto &item : items) {}", "ptr.nn" becomes
// "if (ptr != nullptr) {}", "Widget.make_unique" becomes
// "std::make_unique<Widget>()". Statements (if, for, return, var...) are
// only offered where the expression starts a statement. For C, C++, Java,
// JavaScript and TypeScript.
class PostfixTemplateProvider : public ICompletionProvider {
public:
    bool handles(const CompletionContext &context) const override;
    void complete(const CompletionContext &context, const Callback &done) override;
    // ".": the templates open after it, without a language server too.
    QString triggerCharacters(const QString &languageId) const override;

    // Start of the expression that ends before the dot at `dot` (bytes of
    // `text`): a chain of names, calls, subscripts and member accesses
    // ("this->items[i].name", "make<int>(x)", "new Foo()"), or a
    // parenthesized expression. -1 if there is none.
    static int expressionStart(const QByteArray &text, int dot);
    // A variable name for the value of `expression`: "getWidget()" →
    // "widget", "items.size()" → "size"; "value" when nothing fits.
    static QString suggestName(const QString &expression);
};

// Live templates: abbreviations that expand into code, offered where a
// statement starts ("fori", "iter", "main", "sout", "log"...). Their
// placeholders are filled in one after the other with Tab.
class LiveTemplateProvider : public ICompletionProvider {
public:
    bool handles(const CompletionContext &context) const override;
    void complete(const CompletionContext &context, const Callback &done) override;
};
