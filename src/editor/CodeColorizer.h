#pragma once

#include <QList>
#include <QString>

#include <memory>

class EditorWidget;

// Syntax categories of a piece of code, as the editor finds them, for text
// outside an editor (the code blocks of the Markdown preview): the same
// highlighters (tree-sitter or QScintilla lexers) run on a hidden editor,
// whose styles are read back.
class CodeColorizer {
public:
    // Part of the code and its category ("keyword", "string.special",
    // "comment"…, as in Theme::syntaxStyles); positions in QString characters.
    struct Span {
        int start;
        int length;
        QString category;
    };

    CodeColorizer();
    ~CodeColorizer();

    // Spans of `code` that have a category; none for an unknown language.
    QList<Span> categorize(const QString &code, const QString &languageId);

    // Language of a code fence's info string ("cpp", "py", "TypeScript",
    // "c++"…), as a LanguageRegistry identifier; plain text if unknown.
    static QString languageOfFence(const QString &info);

private:
    std::unique_ptr<EditorWidget> editor_;
    QStringList categories_; // index → category, from the probe theme
};
