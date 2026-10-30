// SPDX-License-Identifier: GPL-3.0-only

#include "MergeTextPanel.h"

#include "MergeGutter.h"
#include "MergePane.h"
#include "ui/AppFonts.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QTimer>

#include <algorithm>
#include <ranges>

namespace {

using ThreeWayMerge::ChunkKind;
using Choice = MergeDocument::Choice;
using Segment = MergeDocument::Segment;
using Shade = MergePane::Shade;

// Rows kept above a conflict the view goes to.
constexpr int rowsAboveConflict = 3;
constexpr int headerMarginPx = 8;
constexpr int typingPauseMs = 1000;

enum class Side {
    Current,
    Incoming
};

bool takes(Choice choice, Side side)
{
    switch (choice) {
    case Choice::Current:
        return side == Side::Current;
    case Choice::Incoming:
        return side == Side::Incoming;
    case Choice::CurrentThenIncoming:
    case Choice::IncomingThenCurrent:
        return true;
    case Choice::Unresolved:
    case Choice::Edited:
    case Choice::Combined:
        return false;
    }
    return false;
}

// The background of a segment in a side's column.
Shade sideShade(const Segment &segment, Side side)
{
    const ChunkKind changedHere = side == Side::Current ? ChunkKind::Current : ChunkKind::Incoming;
    switch (segment.chunk.kind) {
    case ChunkKind::Unchanged:
        return Shade::None;
    case ChunkKind::BothSame:
        return Shade::Merged;
    case ChunkKind::Current:
    case ChunkKind::Incoming:
        return segment.chunk.kind == changedHere ? Shade::Merged : Shade::None;
    case ChunkKind::Conflict:
        break;
    }
    if (segment.choice == Choice::Unresolved)
        return Shade::Conflict;
    if (!takes(segment.choice, side))
        return Shade::None;
    return side == Side::Current ? Shade::Current : Shade::Incoming;
}

// The background of a segment in the result's column.
Shade resultShade(const Segment &segment)
{
    switch (segment.choice) {
    case Choice::Unresolved:
        return Shade::Conflict;
    case Choice::Edited:
        return Shade::Edited;
    case Choice::Current:
        return segment.chunk.kind == ChunkKind::Conflict ? Shade::Current : Shade::Merged;
    case Choice::Incoming:
        return segment.chunk.kind == ChunkKind::Conflict ? Shade::Incoming : Shade::Merged;
    case Choice::CurrentThenIncoming:
    case Choice::IncomingThenCurrent:
    case Choice::Combined:
        return Shade::Merged;
    }
    return Shade::None;
}

// Unchanged lines have no shade wherever they are.
Shade unlessUnchanged(const Segment &segment, Shade shade)
{
    const bool isUntouched = segment.chunk.kind == ChunkKind::Unchanged && segment.choice != Choice::Edited;
    return isUntouched ? Shade::None : shade;
}

// The choice that adds `side`'s lines to what `segment` takes: that side
// alone, or after the other side already taken.
Choice addingSide(const Segment &segment, Side side)
{
    const Choice alone = side == Side::Current ? Choice::Current : Choice::Incoming;
    const Choice other = side == Side::Current ? Choice::Incoming : Choice::Current;
    if (segment.choice != other)
        return alone;
    return side == Side::Current ? Choice::IncomingThenCurrent : Choice::CurrentThenIncoming;
}

int rowHeightOf(const MergePane &pane)
{
    return int(pane.SendScintilla(QsciScintillaBase::SCI_TEXTHEIGHT, 0L));
}

} // namespace

MergeTextPanel::MergeTextPanel(const Theme &theme, const File &file, QWidget *parent)
    : QWidget(parent)
    , document_(*file.document)
    , current_(new MergePane(theme, file.path, this))
    , result_(new MergePane(theme, file.path, this))
    , incoming_(new MergePane(theme, file.path, this))
    , currentGutter_(new MergeGutter(MergeGutter::Direction::Right, file.sides.current, theme, this))
    , incomingGutter_(new MergeGutter(MergeGutter::Direction::Left, file.sides.incoming, theme, this))
    , typingPause_(new QTimer(this))
{
    layOut(file.sides);
    result_->makeEditable();
    // A pause in the typing ends what one undo takes back.
    typingPause_->setSingleShot(true);
    typingPause_->setInterval(typingPauseMs);
    connect(typingPause_, &QTimer::timeout, this, [this] { document_.finishTyping(); });
    // One scroll bar, at the right, for the three columns.
    current_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    result_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connectPanes();
    addShortcuts();
    applyTheme(theme);
    refresh();
}

void MergeTextPanel::layOut(const Integration::ConflictSides &sides)
{
    auto *layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createSideHeader(sides.current, tr("yours"), Choice::Current), 0, 0);
    layout->addWidget(createHeader(tr("Result"), tr("type to edit it")), 0, 2);
    layout->addWidget(createSideHeader(sides.incoming, tr("incoming"), Choice::Incoming), 0, 4);
    layout->addWidget(current_, 1, 0);
    layout->addWidget(currentGutter_, 1, 1);
    layout->addWidget(result_, 1, 2);
    layout->addWidget(incomingGutter_, 1, 3);
    layout->addWidget(incoming_, 1, 4);
    for (const int column : {0, 2, 4})
        layout->setColumnStretch(column, 1);
    layout->setRowStretch(1, 1);
}

void MergeTextPanel::goToConflict(Direction direction)
{
    const QList<int> conflicts = conflictSegments();
    if (conflicts.isEmpty())
        return;
    const int top = result_->firstVisibleLine() + rowsAboveConflict;
    int target = direction == Direction::Next ? conflicts.first() : conflicts.last();
    if (direction == Direction::Next) {
        const auto after = std::ranges::find_if(conflicts, [this, top](int i) { return segmentRows_[i] > top; });
        if (after != conflicts.end())
            target = *after;
    } else {
        const auto before =
            std::ranges::find_if(conflicts | std::views::reverse, [this, top](int i) { return segmentRows_[i] < top; });
        if (before != (conflicts | std::views::reverse).end())
            target = *before;
    }
    result_->setFirstVisibleLine(std::max(0, segmentRows_[target] - rowsAboveConflict));
}

int MergeTextPanel::resolveSimpleConflicts()
{
    const int count = document_.resolveSimpleConflicts();
    if (count > 0)
        refresh();
    return count;
}

void MergeTextPanel::applyTheme(const Theme &theme)
{
    for (MergePane *pane : {current_, result_, incoming_})
        pane->applyTheme(theme);
    currentGutter_->applyTheme(theme);
    incomingGutter_->applyTheme(theme);
    QPalette header = palette();
    header.setColor(QPalette::Window, theme.window);
    setPalette(header);
    setAutoFillBackground(true);
    updateMarks();
}

QWidget *MergeTextPanel::createHeader(const QString &name, const QString &role)
{
    auto *header = new QWidget(this);
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(headerMarginPx, headerMarginPx, headerMarginPx, headerMarginPx);
    auto *nameLabel = new QLabel(name, header);
    AppFonts::setInterfaceFont(nameLabel, 1.0, QFont::DemiBold);
    nameLabel->setTextFormat(Qt::PlainText);
    auto *roleLabel = new QLabel(role, header);
    roleLabel->setObjectName(QStringLiteral("pageDescription"));
    layout->addWidget(nameLabel);
    layout->addWidget(roleLabel);
    layout->addStretch();
    return header;
}

QWidget *MergeTextPanel::createSideHeader(const QString &name, const QString &role, Choice side)
{
    QWidget *header = createHeader(name, role);
    auto *takeAll = new QPushButton(tr("Take All"), header);
    takeAll->setToolTip(tr("Take %1's lines for every conflict still open").arg(name));
    connect(takeAll, &QPushButton::clicked, this, [this, side] {
        document_.resolveRemaining(side);
        refresh();
    });
    header->layout()->addWidget(takeAll);
    return header;
}

void MergeTextPanel::connectPanes()
{
    connect(result_, &QsciScintillaBase::SCN_MODIFIED, this, [this](int position, int type) {
        const bool changesText =
            (type & (QsciScintillaBase::SC_MOD_INSERTTEXT | QsciScintillaBase::SC_MOD_DELETETEXT)) != 0;
        if (isShowing_ || !changesText)
            return;
        const int line = int(result_->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, long(position)));
        firstEditedLine_ = firstEditedLine_ < 0 ? line : std::min(firstEditedLine_, line);
    });
    connect(result_, &QsciScintilla::textChanged, this, &MergeTextPanel::onResultEdited);
    for (const MergePane *pane : {current_, result_, incoming_}) {
        connect(pane, &QsciScintillaBase::SCN_UPDATEUI, this, [this, pane] {
            alignWith(*pane);
            updateMarks();
        });
        connect(pane, &QsciScintillaBase::SCN_ZOOM, this, [this, pane] {
            const int zoom = int(pane->SendScintilla(QsciScintillaBase::SCI_GETZOOM));
            for (MergePane *other : {current_, result_, incoming_}) {
                if (other->SendScintilla(QsciScintillaBase::SCI_GETZOOM) != zoom)
                    other->zoomTo(zoom);
            }
            updateMarks();
        });
    }
    connect(currentGutter_, &MergeGutter::takeRequested, this, [this](int segment) { take(segment, Choice::Current); });
    connect(incomingGutter_, &MergeGutter::takeRequested, this,
            [this](int segment) { take(segment, Choice::Incoming); });
}

void MergeTextPanel::addShortcuts()
{
    const auto add = [this](const QKeySequence &keys, auto slot) {
        const auto *shortcut = new QShortcut(keys, this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, slot);
    };
    add(QKeySequence(Qt::Key_F7), [this] { goToConflict(Direction::Next); });
    add(QKeySequence(Qt::SHIFT | Qt::Key_F7), [this] { goToConflict(Direction::Previous); });
    add(QKeySequence::Undo, [this] {
        document_.undo();
        refresh();
    });
    add(QKeySequence::Redo, [this] {
        document_.redo();
        refresh();
    });
}

void MergeTextPanel::take(int segment, Choice side)
{
    const Side taken = side == Choice::Current ? Side::Current : Side::Incoming;
    document_.choose(segment, addingSide(document_.segments().at(segment), taken));
    refresh();
}

void MergeTextPanel::onResultEdited()
{
    if (isShowing_ || firstEditedLine_ < 0)
        return;
    const QByteArrayList typed = ThreeWayMerge::lines(result_->bytes());
    const QByteArrayList before = ThreeWayMerge::lines(document_.resultText());
    // What changed lies between the lines the two have in common at the
    // start, up to the first line the edit touched, and at the end.
    const qsizetype shorter = std::min(typed.size(), before.size());
    qsizetype prefix = 0;
    while (prefix < std::min<qsizetype>(shorter, firstEditedLine_) && typed[prefix] == before[prefix])
        ++prefix;
    qsizetype suffix = 0;
    while (suffix < shorter - prefix && typed[typed.size() - 1 - suffix] == before[before.size() - 1 - suffix])
        ++suffix;
    firstEditedLine_ = -1;
    document_.replaceResultLines(int(prefix), int(before.size() - prefix - suffix),
                                 typed.mid(prefix, typed.size() - prefix - suffix));
    typingPause_->start();
    refresh();
}

void MergeTextPanel::refresh()
{
    const int top = result_->firstVisibleLine();
    QList<MergePane::Stretch> currentStretches;
    QList<MergePane::Stretch> resultStretches;
    QList<MergePane::Stretch> incomingStretches;
    segmentRows_.clear();
    int row = 0;
    const QList<Segment> &segments = document_.segments();
    for (int i = 0; i < segments.size(); ++i) {
        const Segment &segment = segments[i];
        const QByteArrayList result = document_.resultLines(i);
        const int rows = int(std::max({segment.chunk.current.size(), result.size(), segment.chunk.incoming.size()}));
        currentStretches.append({segment.chunk.current, sideShade(segment, Side::Current), rows});
        resultStretches.append({result, unlessUnchanged(segment, resultShade(segment)), rows});
        incomingStretches.append({segment.chunk.incoming, sideShade(segment, Side::Incoming), rows});
        segmentRows_.append(row);
        row += rows;
    }
    isShowing_ = true;
    current_->showStretches(currentStretches);
    result_->showStretches(resultStretches);
    incoming_->showStretches(incomingStretches);
    isShowing_ = false;
    result_->setFirstVisibleLine(top);
    alignWith(*result_);
    updateMarks();
    emit documentChanged();
}

void MergeTextPanel::updateMarks()
{
    const int rowHeight = rowHeightOf(*result_);
    const int top = result_->firstVisibleLine();
    const QList<Segment> &segments = document_.segments();
    // Before the first refresh(), the rows are not known yet.
    if (segmentRows_.size() != segments.size())
        return;
    QList<MergeGutter::Mark> currentMarks;
    QList<MergeGutter::Mark> incomingMarks;
    for (const int i : conflictSegments()) {
        const int y = (segmentRows_[i] - top) * rowHeight;
        currentMarks.append({i, y, takes(segments[i].choice, Side::Current)});
        incomingMarks.append({i, y, takes(segments[i].choice, Side::Incoming)});
    }
    currentGutter_->setMarks(currentMarks, rowHeight);
    incomingGutter_->setMarks(incomingMarks, rowHeight);
}

void MergeTextPanel::alignWith(const MergePane &leader)
{
    const int top = leader.firstVisibleLine();
    for (MergePane *pane : {current_, result_, incoming_}) {
        if (pane != &leader && pane->firstVisibleLine() != top)
            pane->setFirstVisibleLine(top);
    }
}

QList<int> MergeTextPanel::conflictSegments() const
{
    QList<int> conflicts;
    const QList<Segment> &segments = document_.segments();
    for (int i = 0; i < segments.size(); ++i) {
        if (segments[i].chunk.kind == ChunkKind::Conflict)
            conflicts.append(i);
    }
    return conflicts;
}
