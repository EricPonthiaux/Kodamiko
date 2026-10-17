#pragma once

#include <QList>
#include <QString>

#include <climits>
#include <functional>

// Code completion: the shared vocabulary of the providers (where the
// candidates come from), the service that asks them, and the popup.

// A change to the text that comes with a candidate (an import to add), in
// the protocol's coordinates: lines from 0, columns in UTF-16 units.
struct TextEdit {
    int startLine = 0;
    int startColumn = 0;
    int endLine = 0;
    int endColumn = 0;
    QString newText;
};

struct CompletionItem {
    enum class Kind { Keyword, Function, Method, Class, Struct, Interface, Enum, Namespace, Type, Field, Variable,
                      Constant, Macro, Module, Property, Snippet, Text };

    QString label;      // shown, and matched against what is typed
    QString insertText; // inserted in place of the typed prefix; empty: the label
    QString detail;     // shown at the right, muted: a type, a file, a signature
    Kind kind = Kind::Text;
    // Added to the match score: sources that know more (a language
    // server) or are closer to the cursor rank higher.
    int priority = 0;
    // The source's own ranking (a server's, with the expected type first):
    // lower first, between candidates of the same score.
    int order = INT_MAX;
    // insertText is a snippet: "printValue(${1:int value})" (see Snippet).
    bool isSnippet = false;
    // Where the insertion starts, in bytes of the context's text, when
    // before the typed prefix: a postfix template replaces the expression
    // before the dot ("items.for" → "for (auto &item : items)"). -1: the
    // prefix's start.
    int replaceStart = -1;
    // Applied with the candidate, elsewhere in the file (imports).
    QList<TextEdit> additionalEdits;
    // When the edits are only known once the candidate is chosen (jdtls's
    // imports): asks for them, and calls back with them.
    using EditsCallback = std::function<void(const QList<TextEdit> &edits)>;
    std::function<void(const EditsCallback &done)> resolveEdits;
    // Shown beside the list while the candidate is selected.
    QString documentation;
    // When the documentation is only sent on demand (jdtls): asks for it
    // once the candidate is selected.
    using TextCallback = std::function<void(const QString &text)>;
    std::function<void(const TextCallback &done)> resolveDocumentation;

    QString textToInsert() const { return insertText.isEmpty() ? label : insertText; }
};

// The kind in words ("method", "class"), for screen readers and the
// documentation beside the list.
QString kindName(CompletionItem::Kind kind);

// Where completion is asked, as the providers see it.
struct CompletionContext {
    QString filePath;
    QString languageId;  // LanguageRegistry identifier
    QByteArray text;     // the editor's text, UTF-8
    int position = 0;    // the cursor, in bytes
    int prefixStart = 0; // start of the identifier being typed, in bytes
    QString prefix;      // text between prefixStart and the cursor
    int line = 0;        // of the cursor, from 0
    int column = 0;      // of the cursor, in characters, from 0
    // The character that opened completion ("." after an object), or 0 when
    // typing an identifier or on Ctrl+Space.
    QChar triggerCharacter;
    bool explicitRequest = false; // Ctrl+Space
};

// A source of candidates. The candidates need not match the prefix: the
// popup filters and ranks them as the user types.
class ICompletionProvider {
public:
    // `incomplete`: the provider kept only part of the candidates (a
    // language server's limit); typing more must ask again.
    using Callback = std::function<void(const QList<CompletionItem> &items, bool incomplete)>;

    virtual ~ICompletionProvider() = default;

    // Whether this provider serves `context` at all (a language server only
    // serves its languages).
    virtual bool handles(const CompletionContext &context) const = 0;
    // Calls `done` once with the candidates, right away or later (a
    // language server answers asynchronously). `done` may be dropped
    // when the request is outdated.
    virtual void complete(const CompletionContext &context, const Callback &done) = 0;
    // Characters that open completion after them, such as "." for members.
    virtual QString triggerCharacters(const QString &languageId) const
    {
        Q_UNUSED(languageId);
        return {};
    }
};
