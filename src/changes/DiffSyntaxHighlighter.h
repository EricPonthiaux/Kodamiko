// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "DiffLayout.h"
#include "ScratchEditors.h"
#include "scintilla/ScintillaView.h"
#include "ui/Theme.h"

#include <QHash>

#include <functional>

// Colors the code of a diff's displayed lines as the editor would, for one
// render: both versions of a file's blocks go through a scratch editor of
// its language, whose colors become styles of the diff. Within a budget of
// bytes, so that a large diff appears at once, uncolored.
class DiffSyntaxHighlighter {
public:
    // `view` shows `lines`; both, and `theme`, must outlive the highlighter.
    DiffSyntaxHighlighter(ScintillaView &view, const Theme &theme, const QList<DiffLine> &lines);

    // Colors the code of the file whose title bar is displayed line
    // `fileStart`, at `path`, if it fits in what remains of the budget.
    void highlightFile(int fileStart, const QString &path);

private:
    // Styles displayed line `line` from the scratch editor's styles of its
    // bytes, through `diffStyle`.
    void styleRuns(int line, QByteArrayView scratchStyles, const std::function<int(int)> &diffStyle);
    // Style of the diff for a syntax color, defined on first use.
    int syntaxStyle(long foreground, bool bold, bool italic);

    ScintillaView &view_;
    const Theme &theme_;
    const QList<DiffLine> &lines_;
    ScratchEditors scratches_;
    qsizetype budget_;
    QHash<qint64, int> syntaxStyles_; // color, bold and italic → style
};
