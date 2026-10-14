#pragma once

#include "ISyntaxHighlighter.h"

#include <QObject>

#include <tree_sitter/api.h>

#include <vector>

struct TreeSitterGrammar;

// Coloration par tree-sitter, branchée sur le « container lexer » de
// Scintilla : Scintilla signale le texte à colorer (SCN_STYLENEEDED), on
// interroge l'arbre syntaxique et on pose les styles octet par octet.
//
// L'arbre est mis à jour incrémentalement : chaque insertion/suppression est
// reportée dans l'arbre (ts_tree_edit), puis le texte est ré-analysé au
// prochain besoin de coloration. Scintilla travaillant en UTF-8 comme
// tree-sitter, positions Scintilla et octets tree-sitter coïncident.
class TreeSitterHighlighter : public QObject, public ISyntaxHighlighter {
    Q_OBJECT

public:
    explicit TreeSitterHighlighter(const TreeSitterGrammar &grammar);
    ~TreeSitterHighlighter() override;

    void attach(QsciScintilla *editor) override;
    void applyTheme(const Theme &theme) override;

    // Colore [start, end[. Public pour les tests ; sinon appelé par Scintilla.
    void styleRange(int start, int end);

private:
    void onModified(int position, int modificationType, const char *text, int length, int linesAdded,
                    int line, int foldLevelNow, int foldLevelPrev, int token, int annotationLinesAdded);
    void onStyleNeeded(int endPosition);

    // Ré-analyse si le texte a changé ; renvoie la première position dont la
    // structure syntaxique a changé (INT_MAX si aucune).
    int reparseIfNeeded();
    TSPoint pointAt(int position) const;
    const char *documentText() const;
    int documentLength() const;

    const TreeSitterGrammar &grammar_;
    QsciScintilla *editor_ = nullptr;
    TSParser *parser_ = nullptr;
    TSTree *tree_ = nullptr;
    TSQueryCursor *cursor_ = nullptr;
    bool treeIsOutdated_ = true;

    // Style Scintilla de chaque capture de la requête (0 = non colorée).
    std::vector<char> styleOfCapture_;
};
