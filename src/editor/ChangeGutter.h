#pragma once

#include "git/LineChanges.h"
#include "ui/Theme.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <optional>

class EditorWidget;
class QFrame;

// Marks, in a margin of the editor, the lines that differ from the file's
// version in the git index, as IntelliJ does: a green bar for added lines,
// blue for modified ones, a red triangle where lines were deleted. Clicking
// a mark shows the previous lines, with Rollback and Show Diff. The marks
// follow the text as it is typed, unsaved changes included.
class ChangeGutter : public QObject {
    Q_OBJECT

public:
    // Margin used by the marks, right of the line numbers.
    static constexpr int margin = 1;

    explicit ChangeGutter(EditorWidget *editor);

    // Rereads the file's version in the index (after a staging, a commit,
    // a branch switch…). Without a repository or for an untracked file,
    // there is nothing to compare with and no mark.
    void reloadBase();
    // Sets the base text directly (in the file's encoding).
    void setBase(const std::optional<QByteArray> &base);
    bool hasBase() const { return base_.has_value(); }

    // Changes of the current text, up to date.
    QList<LineChange> changes();
    std::optional<LineChange> changeAt(int line);

    // Replaces the change's lines by those of the base; Ctrl+Z undoes it.
    void rollback(const LineChange &change);
    // Rollback of the change at the cursor line. Returns false if there is none.
    bool rollbackAtCursor();
    // Moves the cursor to the next or previous change, wrapping around.
    // Returns false if there are no changes.
    bool goToNext();
    bool goToPrevious();
    // Popup with the base lines of the change at `line`, under that line.
    void showChangeAt(int line);
    // Unified diff of a change, for Show Diff.
    QString diffText(const LineChange &change) const;

    void applyTheme(const Theme &theme);

signals:
    // Show Diff, for the editor tabs.
    void diffRequested(const QString &title, const QString &diffText);

private:
    void scheduleUpdate();
    void update();
    void defineMarkers();
    bool goTo(bool forward);
    int cursorLine() const;

    EditorWidget *editor_;
    Theme theme_;
    std::optional<QByteArray> base_; // UTF-8, like the editor's text
    QList<LineChange> changes_;
    bool upToDate_ = true;
    QTimer timer_;
    QPointer<QFrame> popup_;
};
