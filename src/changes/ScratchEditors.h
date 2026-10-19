#pragma once

#include "syntax/ISyntaxHighlighter.h"
#include "ui/Theme.h"

#include <Qsci/qsciscintilla.h>

#include <map>
#include <memory>

// Off-screen editors that color code, one per language, for one render:
// creating an editor and its highlighter for each file would cost more than
// coloring the file.
class ScratchEditors {
public:
    // `theme` must outlive the editors.
    explicit ScratchEditors(const Theme &theme);

    // The editor coloring `language`, owned by this object; nullptr if the
    // language has no highlighter.
    QsciScintilla *forLanguage(const QString &language);

private:
    // The highlighter goes before the editor it is attached to.
    struct Scratch {
        std::unique_ptr<QsciScintilla> editor;
        std::unique_ptr<ISyntaxHighlighter> highlighter;
    };

    const Theme &theme_;
    std::map<QString, Scratch> scratches_;
};
