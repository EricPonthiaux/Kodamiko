// SPDX-License-Identifier: GPL-3.0-only

#include "ContentMatcher.h"

#include "LibGit2.h"

#include <QList>
#include <QObject>

#include <algorithm>

using libgit2::check;
using libgit2::Ptr;

namespace {

// An added or removed line of a patch, without its line break; it points
// into the patch.
struct PatchLine {
    QByteArrayView text;
    bool isAdded = false;
};

// The tree of the first parent of `commit`; nullptr for the first commit.
Ptr<git_tree> parentTree(const git_commit *commit)
{
    if (git_commit_parentcount(commit) == 0)
        return nullptr;
    git_commit *parent = nullptr;
    check(git_commit_parent(&parent, commit, 0), "Reading a parent commit");
    const Ptr<git_commit> parentGuard(parent);
    git_tree *tree = nullptr;
    check(git_commit_tree(&tree, parent), "Reading a parent commit");
    return Ptr<git_tree>(tree);
}

QList<PatchLine> changedLines(git_patch *patch)
{
    QList<PatchLine> lines;
    for (size_t hunk = 0; hunk < git_patch_num_hunks(patch); ++hunk) {
        const int lineCount = git_patch_num_lines_in_hunk(patch, hunk);
        for (int i = 0; i < lineCount; ++i) {
            const git_diff_line *line = nullptr;
            check(git_patch_get_line_in_hunk(&line, patch, hunk, size_t(i)), "Searching the changes");
            const bool isAdded = line->origin == GIT_DIFF_LINE_ADDITION;
            if (!isAdded && line->origin != GIT_DIFF_LINE_DELETION)
                continue;
            QByteArrayView text(line->content, qsizetype(line->content_len));
            if (text.endsWith('\n'))
                text.chop(1);
            lines.append({text, isAdded});
        }
    }
    return lines;
}

} // namespace

ContentMatcher::ContentMatcher(const ContentSearch &search)
    : match_(search.match)
    , text_(search.pattern.toUtf8())
{
    if (match_ != ContentMatch::ChangedLine)
        return;
    expression_.setPattern(search.pattern);
    if (!expression_.isValid())
        throw GitError(
            QObject::tr("Invalid regular expression \"%1\": %2").arg(search.pattern, expression_.errorString()));
}

bool ContentMatcher::matches(git_repository *repository, const git_commit *commit, const QString &pathspec) const
{
    if (git_commit_parentcount(commit) > 1)
        return false;
    git_tree *tree = nullptr;
    check(git_commit_tree(&tree, commit), "Reading a commit");
    const Ptr<git_tree> treeGuard(tree);
    const Ptr<git_tree> parent = parentTree(commit);

    QByteArray pathBytes = pathspec.toUtf8();
    char *path = pathBytes.data();
    git_diff_options options;
    git_diff_options_init(&options, GIT_DIFF_OPTIONS_VERSION);
    options.context_lines = 0; // only the changed lines are read
    if (!pathspec.isEmpty()) {
        options.pathspec.strings = &path;
        options.pathspec.count = 1;
    }
    git_diff *diff = nullptr;
    check(git_diff_tree_to_tree(&diff, repository, parent.get(), tree, &options), "Searching the changes");
    const Ptr<git_diff> diffGuard(diff);
    for (size_t i = 0; i < git_diff_num_deltas(diff); ++i) {
        git_patch *patch = nullptr;
        check(git_patch_from_diff(&patch, diff, i), "Searching the changes");
        const Ptr<git_patch> patchGuard(patch);
        if (patch && patchMatches(patch))
            return true;
    }
    return false;
}

// The changes of one file. For Occurrences, the text occurs as many times
// before and after the commit when the added lines hold it as often as the
// removed lines (the unchanged lines count the same on both sides).
bool ContentMatcher::patchMatches(git_patch *patch) const
{
    const QList<PatchLine> lines = changedLines(patch);
    if (match_ == ContentMatch::ChangedLine) {
        return std::ranges::any_of(lines, [this](const PatchLine &line) {
            return expression_.match(QString::fromUtf8(line.text)).hasMatch();
        });
    }
    qsizetype addedMinusRemoved = 0;
    for (const PatchLine &line : lines) {
        const qsizetype occurrences = line.text.count(text_);
        addedMinusRemoved += line.isAdded ? occurrences : -occurrences;
    }
    return addedMinusRemoved != 0;
}
