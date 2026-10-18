#pragma once

#include "ISyntaxHighlighter.h"

#include <QtGlobal>

#include <memory>

class QsciLexer;

// Highlighting through a lexer built into QScintilla, for auxiliary files
// (JSON, CMake, Markdown, XML...) that have no tree-sitter grammar.
class LexillaHighlighter : public ISyntaxHighlighter {
    Q_DISABLE_COPY_MOVE(LexillaHighlighter)
public:
    explicit LexillaHighlighter(std::unique_ptr<QsciLexer> lexer);
    ~LexillaHighlighter() override;

    void attach(QsciScintilla *editor) override;
    void applyTheme(const Theme &theme) override;

private:
    std::unique_ptr<QsciLexer> lexer_;
    QsciScintilla *editor_ = nullptr;
};
