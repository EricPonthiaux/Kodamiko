// SPDX-License-Identifier: GPL-3.0-only

#include "ScratchEditors.h"

#include "syntax/HighlighterFactory.h"

ScratchEditors::ScratchEditors(const Theme &theme)
    : theme_(theme)
{
}

ScintillaView *ScratchEditors::forLanguage(const QString &language)
{
    if (const auto it = scratches_.find(language); it != scratches_.end())
        return it->second.editor.get();
    Scratch scratch{std::make_unique<ScintillaView>(), HighlighterFactory::create(language)};
    if (scratch.highlighter) {
        scratch.highlighter->attach(scratch.editor.get());
        scratch.highlighter->applyTheme(theme_);
    } else {
        scratch.editor.reset();
    }
    return scratches_.emplace(language, std::move(scratch)).first->second.editor.get();
}
