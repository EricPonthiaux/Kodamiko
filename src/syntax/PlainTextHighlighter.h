#pragma once

#include "ISyntaxHighlighter.h"

// No highlighting: only the theme's font and base colors.
class PlainTextHighlighter : public ISyntaxHighlighter {
public:
    void attach(QsciScintilla *editor) override;
    void applyTheme(const Theme &theme) override;

    void flush() override { } // nothing deferred

private:
    QsciScintilla *editor_ = nullptr;
};
