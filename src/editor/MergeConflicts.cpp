// SPDX-License-Identifier: GPL-3.0-only

#include "MergeConflicts.h"

#include "ConflictActionBar.h"
#include "EditorWidget.h"
#include "ScrollBarMarks.h"
#include "git/ChangeKindStyle.h"

#include <QScrollBar>

#include <algorithm>
#include <array>
#include <ranges>
#include <utility>

namespace {

// Scintilla marker numbers: after the change marks' (ChangeGutter), before
// the folds'. Each colors the background of its lines.
constexpr int currentMarkerLineMarker = 16;
constexpr int currentLinesMarker = 17;
constexpr int incomingMarkerLineMarker = 18;
constexpr int incomingLinesMarker = 19;
constexpr int baseLinesMarker = 23;
constexpr std::array allMarkers = {currentMarkerLineMarker, currentLinesMarker, incomingMarkerLineMarker,
                                   incomingLinesMarker, baseLinesMarker};
// Typing a marker does not make a conflict at every keystroke.
constexpr int updateDelayMs = 100;
// How much of the side's color tints the background: the lines, their
// marker.
constexpr qreal linesTint = 0.2;
constexpr qreal markerLineTint = 0.4;
constexpr qreal baseTint = 0.1;

QColor blend(const QColor &background, const QColor &tint, qreal amount)
{
    return QColor::fromRgbF(float(background.redF() + (tint.redF() - background.redF()) * amount),
                            float(background.greenF() + (tint.greenF() - background.greenF()) * amount),
                            float(background.blueF() + (tint.blueF() - background.blueF()) * amount));
}

// The colors of the changes' list: green for what is there, blue for what
// comes.
QColor currentColor()
{
    return changeKindColor(ChangeKind::Added);
}

QColor incomingColor()
{
    return changeKindColor(ChangeKind::Renamed);
}

int textLeft(const EditorWidget &editor)
{
    const int margins = static_cast<int>(editor.SendScintilla(QsciScintillaBase::SCI_GETMARGINS));
    int width = 0;
    for (int margin = 0; margin < margins; ++margin)
        width += editor.marginWidth(margin);
    return width;
}

} // namespace

MergeConflicts::MergeConflicts(EditorWidget *editor, Theme theme)
    : QObject(editor)
    , editor_(editor)
    , marks_(new ScrollBarMarks(editor->verticalScrollBar()))
    , theme_(std::move(theme))
{
    update_.setSingleShot(true);
    update_.setInterval(updateDelayMs);
    connect(&update_, &QTimer::timeout, this, &MergeConflicts::refreshNow);
    connect(editor_, &QsciScintilla::textChanged, &update_, qOverload<>(&QTimer::start));
    // On every scroll, edit and caret move.
    connect(editor_, &QsciScintillaBase::SCN_UPDATEUI, this, &MergeConflicts::placeActionBars);
    defineMarkers();
}

void MergeConflicts::applyTheme(const Theme &theme)
{
    theme_ = theme;
    defineMarkers();
    for (ConflictActionBar *bar : std::as_const(actionBars_))
        bar->applyTheme(theme_);
    markScrollBar();
    placeActionBars();
}

void MergeConflicts::defineMarkers()
{
    const QColor &background = theme_.background;
    const auto define = [this](int marker, const QColor &color) {
        editor_->markerDefine(QsciScintilla::Background, marker);
        editor_->setMarkerBackgroundColor(color, marker);
    };
    define(currentMarkerLineMarker, blend(background, currentColor(), markerLineTint));
    define(currentLinesMarker, blend(background, currentColor(), linesTint));
    define(incomingMarkerLineMarker, blend(background, incomingColor(), markerLineTint));
    define(incomingLinesMarker, blend(background, incomingColor(), linesTint));
    define(baseLinesMarker, blend(background, theme_.mutedText, baseTint));
}

std::optional<int> MergeConflicts::conflictAt(int position) const
{
    const auto holds = [position](const ConflictMarkers::Conflict &conflict) {
        return position >= conflict.span.start && position < conflict.span.end;
    };
    const auto found = std::ranges::find_if(conflicts_, holds);
    if (found == conflicts_.end())
        return std::nullopt;
    return int(found - conflicts_.begin());
}

void MergeConflicts::resolve(int index, ConflictMarkers::Resolution resolution)
{
    // The conflicts must match the text being replaced.
    if (update_.isActive())
        refreshNow();
    if (index < 0 || index >= conflicts_.size() || editor_->isReadOnly())
        return;
    const ConflictMarkers::Replacement replacement =
        ConflictMarkers::resolve(editor_->utf8Bytes(), conflicts_.at(index), resolution);
    editor_->SendScintilla(QsciScintillaBase::SCI_SETTARGETRANGE, static_cast<unsigned long>(replacement.span.start),
                           static_cast<long>(replacement.span.end));
    editor_->SendScintilla(QsciScintillaBase::SCI_REPLACETARGET, uintptr_t(replacement.text.size()),
                           replacement.text.constData());
    editor_->SendScintilla(QsciScintillaBase::SCI_GOTOPOS, static_cast<unsigned long>(replacement.span.start));
    refreshNow();
}

bool MergeConflicts::goTo(Direction direction)
{
    if (update_.isActive())
        refreshNow();
    if (conflicts_.isEmpty())
        return false;
    int line = 0;
    int index = 0;
    editor_->getCursorPosition(&line, &index);
    const auto startsAfter = [line](const ConflictMarkers::Conflict &conflict) { return conflict.startLine > line; };
    const auto startsBefore = [line](const ConflictMarkers::Conflict &conflict) { return conflict.startLine < line; };
    int target = 0;
    switch (direction) {
    case Direction::Next: {
        const auto next = std::ranges::find_if(conflicts_, startsAfter);
        target = next == conflicts_.end() ? conflicts_.first().startLine : next->startLine;
        break;
    }
    case Direction::Previous: {
        const auto reversed = conflicts_ | std::views::reverse;
        const auto previous = std::ranges::find_if(reversed, startsBefore);
        target = previous == reversed.end() ? conflicts_.last().startLine : previous->startLine;
        break;
    }
    }
    editor_->setCursorPosition(target, 0);
    editor_->ensureLineVisible(target);
    editor_->SendScintilla(QsciScintillaBase::SCI_VERTICALCENTRECARET);
    return true;
}

ConflictActionBar *MergeConflicts::actionBar(int index) const
{
    return actionBars_.value(index);
}

void MergeConflicts::refreshNow()
{
    update_.stop();
    QList<ConflictMarkers::Conflict> found = ConflictMarkers::find(editor_->utf8Bytes());
    if (found == conflicts_)
        return;
    conflicts_ = std::move(found);
    show();
}

void MergeConflicts::show()
{
    markLines();
    updateActionBarCount();
    placeActionBars();
    markScrollBar();
}

void MergeConflicts::markLines()
{
    for (const int marker : allMarkers)
        editor_->markerDeleteAll(marker);
    // Lines [first, last[.
    const auto mark = [this](int first, int last, int marker) {
        for (int line = first; line < last; ++line)
            editor_->markerAdd(line, marker);
    };
    for (const ConflictMarkers::Conflict &conflict : std::as_const(conflicts_)) {
        const int currentEnd = conflict.baseLine.value_or(conflict.separatorLine);
        editor_->markerAdd(conflict.startLine, currentMarkerLineMarker);
        mark(conflict.startLine + 1, currentEnd, currentLinesMarker);
        mark(currentEnd, conflict.separatorLine + 1, baseLinesMarker);
        mark(conflict.separatorLine + 1, conflict.endLine, incomingLinesMarker);
        editor_->markerAdd(conflict.endLine, incomingMarkerLineMarker);
    }
}

void MergeConflicts::updateActionBarCount()
{
    const qsizetype wanted = editor_->isReadOnly() ? 0 : conflicts_.size();
    while (actionBars_.size() > wanted) {
        // It may be the one whose link resolved the conflict, still in its
        // click.
        ConflictActionBar *bar = actionBars_.takeLast();
        bar->hide();
        bar->deleteLater();
    }
    while (actionBars_.size() < wanted) {
        auto *bar = new ConflictActionBar(theme_, editor_->viewport());
        connect(bar, &ConflictActionBar::resolutionChosen, this, [this, bar](ConflictMarkers::Resolution resolution) {
            const qsizetype index = actionBars_.indexOf(bar);
            if (index >= 0)
                resolve(int(index), resolution);
        });
        actionBars_.append(bar);
    }
}

void MergeConflicts::placeActionBars()
{
    const int left = textLeft(*editor_);
    const int viewportHeight = editor_->viewport()->height();
    for (qsizetype index = 0; index < actionBars_.size(); ++index) {
        ConflictActionBar *bar = actionBars_.at(index);
        const int line = conflicts_.at(index).startLine;
        const bool isLineShown =
            line < editor_->lines() && editor_->SendScintilla(QsciScintillaBase::SCI_GETLINEVISIBLE, line) != 0;
        const long lineEnd = editor_->SendScintilla(QsciScintillaBase::SCI_GETLINEENDPOSITION, line);
        const int x = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POINTXFROMPOSITION, 0, lineEnd));
        const int y = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POINTYFROMPOSITION, 0, lineEnd));
        const int height = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_TEXTHEIGHT, line));
        const bool isInView = isLineShown && y + height > 0 && y < viewportHeight;
        if (!isInView) {
            bar->hide();
            continue;
        }
        bar->move(std::max(x, left), y + (height - bar->height()) / 2);
        bar->show();
    }
}

void MergeConflicts::markScrollBar()
{
    if (conflicts_.isEmpty()) {
        marks_->clear();
        return;
    }
    const qreal lineCount = std::max(1, editor_->lines());
    QList<qreal> positions;
    for (const ConflictMarkers::Conflict &conflict : std::as_const(conflicts_))
        positions.append(conflict.startLine / lineCount);
    marks_->setMarks(positions, changeKindColor(ChangeKind::Conflicted));
}
