#pragma once

#include "ISyntaxHighlighter.h"

// Aucune coloration : seulement la police et les couleurs de base du thème.
class PlainTextHighlighter : public ISyntaxHighlighter {
public:
    void attach(QsciScintilla *editor) override;
    void applyTheme(const Theme &theme) override;

private:
    QsciScintilla *editor_ = nullptr;
};
