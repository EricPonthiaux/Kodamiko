#pragma once

#include "Completion.h"

#include <QStringList>

#include <vector>

class SymbolIndex;

// Completion without a language server: what can be known from the text.

// Identifiers of the current file, nearest to the cursor first; in
// extended completion, those of the other open files too.
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

    // "true", "nullptr"... where their type is expected.
    bool servesSmart() const override { return true; }

    static QStringList keywords(const QString &languageId);
    // The keywords that fit at `position` of `text`, as IntelliJ offers
    // them: in Java and JavaScript, modifiers in a class body but no
    // "return", statements in code but no "private", only declarations
    // at the top of a Java file. The language's keywords elsewhere.
    static QStringList keywordsAt(const QString &languageId, const QByteArray &text, int position);
};

// Classes, functions, fields... of every file of the project (see
// SymbolIndex), with their container or file as detail.
class ProjectSymbolsProvider : public ICompletionProvider {
public:
    explicit ProjectSymbolsProvider(const SymbolIndex *index);
    bool handles(const CompletionContext &context) const override;
    void complete(const CompletionContext &context, const Callback &done) override;

private:
    struct Candidate {
        CompletionItem item;
        QString filePath;
    };

    const SymbolIndex *index_;
    // The project's symbols of one language family, deduplicated: rebuilt
    // only when the index changes, not at every keystroke.
    std::vector<Candidate> candidates_;
    QString candidatesFamily_;
    quint64 candidatesRevision_ = 0;
    bool candidatesValid_ = false;
};

// Whether `c` belongs to an identifier.
bool isIdentifierCharacter(QChar c);
