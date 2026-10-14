#pragma once

#include "ISyntaxHighlighter.h"

#include <memory>

class QsciLexer;

// Coloration par un lexer intégré à QScintilla, pour les fichiers annexes
// (JSON, CMake, Markdown, XML...) qui n'ont pas de grammaire tree-sitter.
class LexillaHighlighter : public ISyntaxHighlighter {
public:
    explicit LexillaHighlighter(std::unique_ptr<QsciLexer> lexer);
    ~LexillaHighlighter() override;

    void attach(QsciScintilla *editor) override;
    void applyTheme(const Theme &theme) override;

private:
    std::unique_ptr<QsciLexer> lexer_;
    QsciScintilla *editor_ = nullptr;
};
