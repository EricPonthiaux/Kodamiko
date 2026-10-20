// ChangesView: the diff pane, where the lines or blocks shown are staged,
// unstaged or discarded from the keyboard, the block buttons or the menu.

#include "ChangesView.h"
#include "DiffHeaderBar.h"
#include "DiffView.h"
#include "DiscardConfirmation.h"
#include "git/GitCommands.h"
#include "git/GitOperations.h"

#include <QMenu>
#include <QShortcut>

#include <tuple>

QHash<int, PatchSelection> ChangesView::diffSelection(int line, bool *lines) const
{
    // Displayed file that contains a diff line, and the position in its patch.
    const auto locate = [this](int diffLine) -> std::tuple<int, int, int> {
        const DiffLocation location = diff_->locate(diffLine);
        return {location.file, location.hunk, location.line};
    };

    QHash<int, PatchSelection> result;
    *lines = false;
    if (diff_->hasSelectedText()) {
        int lineFrom = 0;
        int indexFrom = 0;
        int lineTo = 0;
        int indexTo = 0;
        diff_->getSelection(&lineFrom, &indexFrom, &lineTo, &indexTo);
        if (indexTo == 0 && lineTo > lineFrom)
            --lineTo; // selection that ends at the start of a line
        for (int l = lineFrom; l <= lineTo; ++l) {
            const auto [file, hunk, hunkLine] = locate(l);
            if (file >= 0 && hunk >= 0 && hunkLine >= 0 && shownPatches_[file].hunks[hunk].isChange(hunkLine))
                result[file][hunk].insert(hunkLine);
        }
        if (!result.isEmpty()) {
            *lines = true;
            return result;
        }
    }
    const auto [file, hunk, hunkLine] = locate(line);
    Q_UNUSED(hunkLine);
    if (file >= 0 && hunk >= 0)
        result[file] = shownPatches_[file].selectHunk(hunk);
    return result;
}

bool ChangesView::canApplyDiffLines() const
{
    return diffHeader_->whitespace() == Whitespace::Compare;
}

void ChangesView::applyDiffSelection(int line, bool discard)
{
    if (!status_)
        return;
    if (!canApplyDiffLines()) {
        emit statusMessage(tr("Show the whitespace changes to stage, unstage or discard lines."));
        return;
    }
    bool lines = false;
    const QHash<int, PatchSelection> selection = diffSelection(line, &lines);
    if (selection.isEmpty())
        return;

    // Stage: patch applied as is to the index. Unstage: applied in reverse
    // to the index. Discard: applied in reverse to the files.
    const PatchDirection direction =
        shownSection_ == Section::Unstaged && !discard ? PatchDirection::Forward : PatchDirection::Reverse;
    QByteArray patch;
    QList<FileRef> touched;
    for (auto it = selection.begin(); it != selection.end(); ++it) {
        patch += shownPatches_[it.key()].partial(it.value(), direction);
        touched.append({shownSection_, it.key()});
    }
    if (patch.isEmpty())
        return;

    if (discard) {
        if (!DiscardConfirmation::ask(this, lines ? tr("Discard the selected lines?") : tr("Discard this hunk?"), {}))
            return;
        commands_->run(tr("Discarding changes"), [info = status_->info, paths = repoPaths(touched), patch] {
            GitOperations::discardPatch(info, paths, patch);
            return QObject::tr("Changes discarded. %1").arg(DiscardConfirmation::hint());
        });
        return;
    }
    commands_->run(shownSection_ == Section::Unstaged ? tr("Staging") : tr("Unstaging"),
                   [info = status_->info, patch, direction] {
                       GitOperations::applyToIndex(info, patch, direction);
                       return QString();
                   });
}

void ChangesView::addDiffShortcuts()
{
    const auto add = [this](const QKeySequence &keys, auto slot) {
        const auto *shortcut = new QShortcut(keys, diff_, nullptr, nullptr, Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, slot);
    };
    add(QKeySequence(Qt::Key_Space), [this] {
        if (!commands_->isBusy())
            applyDiffSelection(diffCursorLine(), false);
    });
    add(QKeySequence(Qt::Key_Delete), [this] {
        if (shownSection_ == Section::Unstaged && !commands_->isBusy())
            applyDiffSelection(diffCursorLine(), true);
    });
    add(QKeySequence(Qt::Key_F4), [this] { openFileAt(diffCursorLine()); });
}

int ChangesView::diffCursorLine() const
{
    int line = 0;
    int index = 0;
    diff_->getCursorPosition(&line, &index);
    return line;
}

void ChangesView::openFileAt(int line)
{
    const int file = diff_->locate(line).file;
    if (!status_ || file < 0)
        return;
    const FileChange &change = files(shownSection_).at(file).change;
    if (change.kind != ChangeKind::Deleted)
        emit openFileRequested(change.path, diff_->newFileLine(line));
}

void ChangesView::showDiffMenu(const QPoint &position)
{
    int line = diff_->lineAt(position);
    if (line < 0) {
        int index = 0;
        diff_->getCursorPosition(&line, &index);
    }
    bool lines = false;
    const bool hasSelection = status_ && !diffSelection(line, &lines).isEmpty();
    const bool busy = commands_->isBusy();

    QMenu *menu = diff_->createStandardContextMenu();
    QAction *first = menu->actions().value(0);
    // The keys of addDiffShortcuts(), shown only: an action's shortcut
    // would take Space from the menu itself.
    const auto addAction = [&](const QString &text, const QKeySequence &keys, auto slot) {
        auto *action = new QAction(text + QLatin1Char('\t') + keys.toString(QKeySequence::NativeText), menu);
        connect(action, &QAction::triggered, this, slot);
        menu->insertAction(first, action);
        return action;
    };
    const bool canApply = hasSelection && !busy && canApplyDiffLines();
    if (shownSection_ == Section::Unstaged) {
        addAction(lines ? tr("Stage Selected Lines") : tr("Stage This Hunk"), QKeySequence(Qt::Key_Space),
                  [this, line] { applyDiffSelection(line, false); })
            ->setEnabled(canApply);
        addAction(lines ? tr("Discard Selected Lines…") : tr("Discard This Hunk…"), QKeySequence(Qt::Key_Delete),
                  [this, line] { applyDiffSelection(line, true); })
            ->setEnabled(canApply);
    } else {
        addAction(lines ? tr("Unstage Selected Lines") : tr("Unstage This Hunk"), QKeySequence(Qt::Key_Space),
                  [this, line] { applyDiffSelection(line, false); })
            ->setEnabled(canApply);
    }
    addAction(tr("Open in Editor"), QKeySequence(Qt::Key_F4), [this, line] {
        openFileAt(line);
    })->setEnabled(status_ && diff_->locate(line).file >= 0);
    menu->insertSeparator(first);
    menu->exec(diff_->viewport()->mapToGlobal(position));
    delete menu;
}
