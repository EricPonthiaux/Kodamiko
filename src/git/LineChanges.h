// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

// Block of lines in which the current text of a file differs from a base
// version (its content in the git index). Line numbers start at 0.
struct LineChange {
    enum class Kind {
        Added,
        Modified,
        Deleted
    };

    int baseStart = 0; // first base line replaced; insertion point if baseCount is 0
    int baseCount = 0;
    int start = 0; // first current line; for a deletion, the line that follows it
    int count = 0;

    Kind kind() const;
    // Whether the change's marker is on `line`: its lines, or for a
    // deletion the line that follows it (the last line at the end of the text).
    bool covers(int line, int lineCount) const;
};

namespace LineChanges {

// Blocks in which `current` differs from `base`, in text order.
QList<LineChange> compute(const QByteArray &base, const QByteArray &current);

// Byte offset of the start of `line` in `text`; text.size() past the end.
qsizetype lineOffset(const QByteArray &text, int line);
// Lines [start, start + count) of `text`, with their line endings.
QByteArray lines(const QByteArray &text, int start, int count);

// `current` with `change` undone: its lines replaced by the base ones.
QByteArray revert(const QByteArray &base, const QByteArray &current, const LineChange &change);

// Contents of the file in the index of its repository, as git would write it
// to disk (line ending conversion included). nullopt if the file belongs to
// no repository or is not tracked. Throws GitError.
std::optional<QByteArray> indexVersion(const QString &absolutePath);

} // namespace LineChanges
