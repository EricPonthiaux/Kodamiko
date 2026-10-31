// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ISyntaxHighlighter.h"

// No highlighting: only the theme's font and base colors.
class PlainTextHighlighter : public ISyntaxHighlighter {
public:
    void attach(ScintillaView *editor) override;
    void applyTheme(const Theme &theme) override;
    QString styleCategory(int style, const Theme &theme) const override;

    void flush() override { } // nothing deferred

private:
    ScintillaView *editor_ = nullptr;
};
