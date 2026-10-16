#include "TreeSitterFolds.h"

#include <Qsci/qsciscintillabase.h>

#include <cstring>
#include <set>
#include <utility>

namespace TreeSitterFolds {
namespace {

// Lines [first, last] hidden when the fold whose header is `first - 1` is
// closed.
using Body = std::pair<uint32_t, uint32_t>;

bool isAnonymous(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && !ts_node_is_named(node) && std::strcmp(ts_node_type(node), type) == 0;
}

bool isBracketed(TSNode node)
{
    const uint32_t count = ts_node_child_count(node);
    if (count < 2)
        return false;
    const TSNode first = ts_node_child(node, 0);
    const TSNode last = ts_node_child(node, count - 1);
    return (isAnonymous(first, "{") && isAnonymous(last, "}")) || (isAnonymous(first, "[") && isAnonymous(last, "]"))
        || (isAnonymous(first, "(") && isAnonymous(last, ")"));
}

bool isPreprocessorConditional(TSNode node)
{
    const TSNode first = ts_node_child(node, 0);
    return !ts_node_is_null(first) && !ts_node_is_named(first) && std::strncmp(ts_node_type(first), "#if", 3) == 0;
}

void collect(TSTreeCursor *cursor, std::set<Body> &bodies)
{
    const TSNode node = ts_tree_cursor_current_node(cursor);
    const uint32_t startLine = ts_node_start_point(node).row;
    const TSPoint end = ts_node_end_point(node);
    // A node ending with its line break ("#endif\n") ends on the line before.
    const uint32_t endLine = end.column == 0 && end.row > startLine ? end.row - 1 : end.row;
    // A node on a single line contains no fold.
    if (startLine == endLine)
        return;

    if (ts_node_is_named(node) && std::strstr(ts_node_type(node), "comment"))
        bodies.insert({startLine + 1, endLine});
    else if (endLine > startLine + 1 && (isBracketed(node) || isPreprocessorConditional(node)))
        bodies.insert({startLine + 1, endLine - 1});

    if (ts_tree_cursor_goto_first_child(cursor)) {
        do {
            collect(cursor, bodies);
        } while (ts_tree_cursor_goto_next_sibling(cursor));
        ts_tree_cursor_goto_parent(cursor);
    }
}

} // namespace

std::vector<int> levels(TSNode root, int lineCount)
{
    // The same lines can be folded by several nodes ("f({" … "});"): they
    // count once.
    std::set<Body> bodies;
    TSTreeCursor cursor = ts_tree_cursor_new(root);
    collect(&cursor, bodies);
    ts_tree_cursor_delete(&cursor);

    std::vector<int> depthChange(lineCount + 1, 0);
    std::vector<bool> isHeader(lineCount, false);
    for (const auto &[first, last] : bodies) {
        if (static_cast<int>(last) >= lineCount)
            continue;
        ++depthChange[first];
        --depthChange[last + 1];
        isHeader[first - 1] = true;
    }

    std::vector<int> result(lineCount);
    int depth = 0;
    for (int line = 0; line < lineCount; ++line) {
        depth += depthChange[line];
        result[line] = (QsciScintillaBase::SC_FOLDLEVELBASE + depth)
                     | (isHeader[line] ? QsciScintillaBase::SC_FOLDLEVELHEADERFLAG : 0);
    }
    return result;
}

} // namespace TreeSitterFolds
