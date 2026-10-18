#pragma once

#include "ISyntaxHighlighter.h"

#include <QObject>
#include <QTimer>

#include <tree_sitter/api.h>

#include <vector>

struct TreeSitterGrammar;

// Tree-sitter highlighting, plugged into Scintilla's "container lexer":
// Scintilla reports the text to style (SCN_STYLENEEDED), we query the syntax
// tree and set the styles byte by byte.
//
// The tree is updated incrementally: each insertion/deletion is applied to
// the tree (ts_tree_edit), then the text is reparsed the next time
// highlighting is needed. Since Scintilla works in UTF-8 like tree-sitter,
// Scintilla positions and tree-sitter bytes coincide.
class TreeSitterHighlighter : public QObject, public ISyntaxHighlighter {
    Q_OBJECT

public:
    explicit TreeSitterHighlighter(const TreeSitterGrammar &grammar);
    ~TreeSitterHighlighter() override;

    void attach(QsciScintilla *editor) override;
    void applyTheme(const Theme &theme) override;
    void flush() override;

    // Styles [start, end[. Public for tests; otherwise called by Scintilla.
    void styleRange(int start, int end);

private:
    void onModified(int position,
                    int modificationType,
                    const char *text,
                    int length,
                    int linesAdded,
                    int line,
                    int foldLevelNow,
                    int foldLevelPrev,
                    int token,
                    int annotationLinesAdded);
    void onStyleNeeded(int endPosition);

    // Reparses if the text changed; returns the first position whose syntax
    // structure changed (INT_MAX if none).
    int reparseIfNeeded();
    // Fold levels of the lines, from the new tree (see TreeSitterFolds).
    void updateFoldLevels();
    TSPoint pointAt(int position) const;
    const char *documentText() const;
    int documentLength() const;

    const TreeSitterGrammar &grammar_;
    QsciScintilla *editor_ = nullptr;
    TSParser *parser_ = nullptr;
    TSTree *tree_ = nullptr;
    TSQueryCursor *cursor_ = nullptr;
    bool treeIsOutdated_ = true;
    // Fold levels walk the whole tree and every line: after an edit, they
    // are updated once typing pauses rather than at each keystroke.
    QTimer foldTimer_;

    // Scintilla style of each query capture (0 = not highlighted).
    std::vector<char> styleOfCapture_;
};
