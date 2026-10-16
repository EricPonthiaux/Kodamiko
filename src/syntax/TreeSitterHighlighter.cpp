#include "TreeSitterHighlighter.h"

#include "HighlightQuery.h"
#include "ScintillaStyles.h"
#include "Theme.h"
#include "TreeSitterFolds.h"
#include "TreeSitterGrammars.h"

#include <Qsci/qsciscintilla.h>

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace {

// Span of text captured by the query, with the style to apply to it.
struct StyledSpan {
    uint32_t start;
    uint32_t end;
    uint32_t patternIndex;
    char style;
};

} // namespace

TreeSitterHighlighter::TreeSitterHighlighter(const TreeSitterGrammar &grammar)
    : grammar_(grammar)
    , parser_(ts_parser_new())
    , cursor_(ts_query_cursor_new())
{
    ts_parser_set_language(parser_, grammar_.language);
}

TreeSitterHighlighter::~TreeSitterHighlighter()
{
    if (tree_)
        ts_tree_delete(tree_);
    ts_query_cursor_delete(cursor_);
    ts_parser_delete(parser_);
}

void TreeSitterHighlighter::attach(QsciScintilla *editor)
{
    editor_ = editor;
    editor_->setLexer(nullptr);
    editor_->SendScintilla(QsciScintillaBase::SCI_SETLEXER, QsciScintillaBase::SCLEX_CONTAINER);

    // BEFOREDELETE is essential: after a deletion, the line/column of the end
    // of the deleted text can no longer be computed.
    const long eventMask = editor_->SendScintilla(QsciScintillaBase::SCI_GETMODEVENTMASK);
    editor_->SendScintilla(QsciScintillaBase::SCI_SETMODEVENTMASK,
                           eventMask | QsciScintillaBase::SC_MOD_INSERTTEXT
                               | QsciScintillaBase::SC_MOD_BEFOREDELETE);

    connect(editor_, &QsciScintillaBase::SCN_MODIFIED, this, &TreeSitterHighlighter::onModified);
    connect(editor_, &QsciScintillaBase::SCN_STYLENEEDED, this, &TreeSitterHighlighter::onStyleNeeded);
}

void TreeSitterHighlighter::applyTheme(const Theme &theme)
{
    ScintillaStyles::applyDefaultStyle(*editor_, theme);
    ScintillaStyles::applySyntaxStyles(*editor_, theme);

    const HighlightQuery &query = grammar_.highlights;
    styleOfCapture_.assign(query.captureCount(), 0);
    for (uint32_t capture = 0; capture < query.captureCount(); ++capture) {
        const int themeIndex = theme.syntaxStyleIndex(query.captureName(capture));
        if (themeIndex >= 0 && themeIndex < ScintillaStyles::maxSyntaxStyles)
            styleOfCapture_[capture] = static_cast<char>(ScintillaStyles::firstSyntaxStyle + themeIndex);
    }

    editor_->recolor();
}

void TreeSitterHighlighter::onModified(int position, int modificationType, const char *, int length, int,
                                       int, int, int, int, int)
{
    // Without a tree, the next parse will be a full one: nothing to apply.
    if (!tree_)
        return;

    TSInputEdit edit{};
    if (modificationType & QsciScintillaBase::SC_MOD_INSERTTEXT) {
        edit.start_byte = position;
        edit.old_end_byte = position;
        edit.new_end_byte = position + length;
        edit.start_point = pointAt(position);
        edit.old_end_point = edit.start_point;
        edit.new_end_point = pointAt(position + length);
    } else if (modificationType & QsciScintillaBase::SC_MOD_BEFOREDELETE) {
        edit.start_byte = position;
        edit.old_end_byte = position + length;
        edit.new_end_byte = position;
        edit.start_point = pointAt(position);
        edit.old_end_point = pointAt(position + length);
        edit.new_end_point = edit.start_point;
    } else {
        return;
    }
    ts_tree_edit(tree_, &edit);
    treeIsOutdated_ = true;
}

void TreeSitterHighlighter::onStyleNeeded(int endPosition)
{
    const int endStyled = editor_->SendScintilla(QsciScintillaBase::SCI_GETENDSTYLED);
    const int firstLine = editor_->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, endStyled);
    const int lineStart = editor_->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, firstLine);

    // An edit can change the meaning of the text before it (typing "(" after
    // an identifier turns it into a function call), so we also recolor from
    // the first structural change reported by tree-sitter.
    const int firstStructuralChange = reparseIfNeeded();
    styleRange(std::min(lineStart, firstStructuralChange), endPosition);
}

int TreeSitterHighlighter::reparseIfNeeded()
{
    if (!treeIsOutdated_)
        return INT_MAX;

    TSTree *newTree = ts_parser_parse_string(parser_, tree_, documentText(), documentLength());
    int firstChange = 0;
    if (tree_) {
        uint32_t rangeCount = 0;
        TSRange *ranges = ts_tree_get_changed_ranges(tree_, newTree, &rangeCount);
        firstChange = INT_MAX;
        for (uint32_t i = 0; i < rangeCount; ++i)
            firstChange = std::min(firstChange, static_cast<int>(ranges[i].start_byte));
        std::free(ranges);
        ts_tree_delete(tree_);
    }
    tree_ = newTree;
    treeIsOutdated_ = false;
    updateFoldLevels();
    return firstChange;
}

void TreeSitterHighlighter::updateFoldLevels()
{
    const int lineCount = editor_->SendScintilla(QsciScintillaBase::SCI_GETLINECOUNT);
    const std::vector<int> levels = TreeSitterFolds::levels(ts_tree_root_node(tree_), lineCount);
    constexpr int levelMask = QsciScintillaBase::SC_FOLDLEVELNUMBERMASK | QsciScintillaBase::SC_FOLDLEVELHEADERFLAG;
    for (int line = 0; line < lineCount; ++line) {
        // Only the lines that change: Scintilla redraws (and may unfold) them.
        if ((editor_->SendScintilla(QsciScintillaBase::SCI_GETFOLDLEVEL, line) & levelMask) != levels[line])
            editor_->SendScintilla(QsciScintillaBase::SCI_SETFOLDLEVEL, line, levels[line]);
    }
}

// Each byte takes the style of the most specific capture covering it:
//  - an inner node wins over the node containing it (an escape sequence
//    inside a string), so we paint from longest to shortest;
//  - for the same node captured by several patterns, the query's last
//    pattern wins, so we paint in pattern order.
void TreeSitterHighlighter::styleRange(int start, int end)
{
    end = std::min(end, documentLength());
    if (start >= end)
        return;
    reparseIfNeeded();

    const char *text = documentText();
    const HighlightQuery &query = grammar_.highlights;

    ts_query_cursor_set_byte_range(cursor_, static_cast<uint32_t>(start), static_cast<uint32_t>(end));
    ts_query_cursor_exec(cursor_, query.query(), ts_tree_root_node(tree_));

    std::vector<StyledSpan> spans;
    TSQueryMatch match;
    uint32_t captureIndex = 0;
    while (ts_query_cursor_next_capture(cursor_, &match, &captureIndex)) {
        const TSQueryCapture &capture = match.captures[captureIndex];
        const char style = styleOfCapture_[capture.index];
        if (style == 0 || !query.matchSatisfiesPredicates(match, text))
            continue;
        spans.push_back({ts_node_start_byte(capture.node), ts_node_end_byte(capture.node), match.pattern_index,
                         style});
    }

    std::stable_sort(spans.begin(), spans.end(), [](const StyledSpan &a, const StyledSpan &b) {
        const uint32_t lengthA = a.end - a.start;
        const uint32_t lengthB = b.end - b.start;
        if (lengthA != lengthB)
            return lengthA > lengthB;
        return a.patternIndex < b.patternIndex;
    });

    std::vector<char> styles(end - start, 0);
    for (const StyledSpan &span : spans) {
        const int from = std::max<int>(span.start, start);
        const int to = std::min<int>(span.end, end);
        std::fill(styles.begin() + (from - start), styles.begin() + std::max(from, to) - start, span.style);
    }

    editor_->SendScintilla(QsciScintillaBase::SCI_STARTSTYLING, start, 0L);
    editor_->SendScintilla(QsciScintillaBase::SCI_SETSTYLINGEX, styles.size(), styles.data());
}

TSPoint TreeSitterHighlighter::pointAt(int position) const
{
    const long line = editor_->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, position);
    const long lineStart = editor_->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, line);
    return TSPoint{static_cast<uint32_t>(line), static_cast<uint32_t>(position - lineStart)};
}

const char *TreeSitterHighlighter::documentText() const
{
    // The pointer stays valid until the next modification of the text.
    return static_cast<const char *>(
        editor_->SendScintillaPtrResult(QsciScintillaBase::SCI_GETCHARACTERPOINTER));
}

int TreeSitterHighlighter::documentLength() const
{
    return editor_->SendScintilla(QsciScintillaBase::SCI_GETLENGTH);
}
