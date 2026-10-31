// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "scintilla/ScintillaView.h"
#include "ui/Theme.h"

#include <QByteArrayList>
#include <QList>

#include <memory>

class ISyntaxHighlighter;

// One column of the merge view (MergeTextPanel): a version of the file,
// stretch after stretch, each stretch padded with blank rows to the height
// it has in the tallest column, so that the three columns face each other
// row for row. A stretch's background says where its lines come from. The
// code is colored as the editor colors the file's language. Read-only,
// unless made editable (the result).
class MergePane : public ScintillaView {
    Q_OBJECT

public:
    // The background of a stretch.
    enum class Shade {
        None,
        Current,  // changed on the current side
        Incoming, // changed on the incoming side
        Merged,   // changed on one side and taken without a conflict
        Conflict, // a conflict still open
        Edited,   // lines the user wrote
        Proposed, // lines the agent proposes, or proposed
    };

    struct Stretch {
        QByteArrayList lines;
        Shade shade = Shade::None;
        // Rows it takes, blank ones included: at least its lines.
        int rows = 0;
    };

    // `path` (any form) chooses the colors of the code.
    MergePane(const Theme &theme, const QString &path, QWidget *parent);
    ~MergePane() override;

    // Lets the user type; the document behind takes care of undoing.
    void makeEditable();
    // The text, as bytes.
    QByteArray bytes() const;
    // Shows `stretches`; keeps the text, and the caret, if it is theirs.
    void showStretches(const QList<Stretch> &stretches);
    void applyTheme(const Theme &theme);

private:
    void defineShades();
    void addPadding(int line, int rows);

    Theme theme_;
    bool isEditable_ = false;
    // Allocated by Scintilla for the blank rows' style.
    int paddingStyle_ = 0;
    std::unique_ptr<ISyntaxHighlighter> highlighter_;
};
