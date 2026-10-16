#pragma once

#include "Completion.h"

#include <QStringList>

class SymbolIndex;

// Completion without a language server: what can be known from the text.

// Identifiers of the current file, nearest to the cursor first.
class DocumentWordsProvider : public ICompletionProvider {
public:
    bool handles(const CompletionContext &context) const override;
    void complete(const CompletionContext &context, const Callback &done) override;

    // Identifiers of `text` (3 characters or more, not starting with a
    // digit), the one at `exclude` (a byte position) left out, ordered by
    // distance to it.
    static QStringList words(const QByteArray &text, int exclude);
};

// Keywords of the file's language.
class KeywordProvider : public ICompletionProvider {
public:
    bool handles(const CompletionContext &context) const override;
    void complete(const CompletionContext &context, const Callback &done) override;

    static QStringList keywords(const QString &languageId);
};

// Classes, functions, fields... of every file of the project (see
// SymbolIndex), with their container or file as detail.
class ProjectSymbolsProvider : public ICompletionProvider {
public:
    explicit ProjectSymbolsProvider(const SymbolIndex *index);
    bool handles(const CompletionContext &context) const override;
    void complete(const CompletionContext &context, const Callback &done) override;

private:
    const SymbolIndex *index_;
};

// Whether `c` belongs to an identifier.
bool isIdentifierCharacter(QChar c);
