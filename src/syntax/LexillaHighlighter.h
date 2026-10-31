// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ISyntaxHighlighter.h"
#include "LexillaLanguages.h"

#include <QtGlobal>

// Highlighting by one of Lexilla's lexers, for auxiliary files (JSON, CMake,
// Markdown, XML...) that have no tree-sitter grammar.
class LexillaHighlighter : public ISyntaxHighlighter {
    Q_DISABLE_COPY_MOVE(LexillaHighlighter)
public:
    // `language` lives as long as the application (LexillaLanguages).
    explicit LexillaHighlighter(const LexillaLanguage &language);
    ~LexillaHighlighter() override = default;

    void attach(ScintillaView *editor) override;
    void applyTheme(const Theme &theme) override;
    QString styleCategory(int style, const Theme &theme) const override;

    void flush() override { } // nothing deferred

private:
    const LexillaLanguage &language_;
    ScintillaView *editor_ = nullptr;
};
