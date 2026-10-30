// SPDX-License-Identifier: GPL-3.0-only

#include "MergeTextPanel.h"

#include "MergeFocusFrame.h"
#include "MergeGutter.h"
#include "MergeOverview.h"
#include "MergePane.h"
#include "MergeProposalBar.h"
#include "ui/AppFonts.h"
#include "ui/GripSplitter.h"
#include "ui/ThemeStyle.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <ranges>

namespace {

using ThreeWayMerge::ChunkKind;
using Choice = MergeDocument::Choice;
using Segment = MergeDocument::Segment;
using Shade = MergePane::Shade;

// After a side taken, the time to see it before the view moves on.
constexpr int moveOnDelayMs = 600;
constexpr int headerMarginPx = 8;
constexpr int typingPauseMs = 1000;
constexpr int columnMinimumWidthPx = 200;
// The columns' widths, the same for every file and session.
const QString columnsKey = QStringLiteral("mergeWindow/columns");

// A column of the panel: its header above its panes, side by side.
QWidget *column(QWidget *header, std::initializer_list<QWidget *> panes, QWidget *parent)
{
    auto *column = new QWidget(parent);
    column->setMinimumWidth(columnMinimumWidthPx);
    auto *row = new QHBoxLayout;
    row->setSpacing(0);
    for (QWidget *pane : panes)
        row->addWidget(pane, pane->maximumWidth() == QWIDGETSIZE_MAX ? 1 : 0);
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addLayout(row, 1);
    return column;
}

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
    case Choice::Suggested:
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
    case Choice::Suggested:
        return Shade::Proposed;
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
    , overview_(new MergeOverview(theme, this))
    , focus_(new MergeFocusFrame(theme, this))
    , proposalBar_(new MergeProposalBar(this))
    , typingPause_(new QTimer(this))
    , agentName_(file.agentName)
{
    layOut(file.sides);
    connectProposalBar();
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
    QWidget *currentHeader = createSideHeader(sides.current, tr("yours"), Choice::Current);
    QWidget *resultHeader = createHeader(tr("Result"), tr("The code to keep · editable"));
    resultHeader->setObjectName(QStringLiteral("mergeResultHeader"));
    QWidget *incomingHeader = createSideHeader(sides.incoming, tr("incoming"), Choice::Incoming);
    // Each column lays out its own header: one height keeps the lines level.
    const int headerHeight = std::max(
        {currentHeader->sizeHint().height(), resultHeader->sizeHint().height(), incomingHeader->sizeHint().height()});
    for (QWidget *header : {currentHeader, resultHeader, incomingHeader})
        header->setFixedHeight(headerHeight);
    headerHeight_ = headerHeight;
    // The gutters stay beside the result, inside the side columns.
    auto *columns = new GripSplitter(Qt::Horizontal, this);
    columns->setChildrenCollapsible(false);
    columns->addWidget(column(currentHeader, {current_, currentGutter_}, columns));
    columns->addWidget(column(resultHeader, {result_}, columns));
    columns->addWidget(column(incomingHeader, {incomingGutter_, incoming_, overview_}, columns));
    if (!columns->restoreState(QSettings().value(columnsKey).toByteArray()))
        columns->setSizes({1, 1, 1}); // equal shares of the width
    connect(columns, &QSplitter::splitterMoved, this, [this, columns] {
        QSettings().setValue(columnsKey, columns->saveState());
        updateFocus();
    });
    columns_ = columns;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(columns, 1);
    layout->addWidget(proposalBar_);
    focus_->raise();
}

void MergeTextPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    focus_->setGeometry(columns_->geometry());
    updateFocus();
}

void MergeTextPanel::goToConflict(Direction direction)
{
    const QList<int> conflicts = conflictSegments();
    if (conflicts.isEmpty())
        return;
    const qsizetype here = conflicts.indexOf(currentSegment_);
    const qsizetype step = direction == Direction::Next ? 1 : conflicts.size() - 1;
    // From none shown: the first, or the last.
    const qsizetype target =
        here < 0 ? (direction == Direction::Next ? 0 : conflicts.size() - 1) : (here + step) % conflicts.size();
    showConflict(conflicts[target]);
}

void MergeTextPanel::goToConflict(int index)
{
    const QList<int> conflicts = conflictSegments();
    if (index >= 0 && index < conflicts.size())
        showConflict(conflicts[index]);
}

void MergeTextPanel::goToNextOpenConflict()
{
    const int next = nextOpenSegment(currentSegment_);
    if (next >= 0)
        showConflict(next);
}

QList<ConflictState> MergeTextPanel::conflictStates() const
{
    QList<ConflictState> states;
    for (const int segment : conflictSegments())
        states.append(stateOf(segment));
    return states;
}

int MergeTextPanel::currentConflict() const
{
    return int(conflictSegments().indexOf(currentSegment_));
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
    overview_->applyTheme(theme);
    focus_->applyTheme(theme);
    currentGutter_->applyTheme(theme);
    incomingGutter_->applyTheme(theme);
    // From the theme, not from palette(): a copy would keep every other
    // color of the theme the panel was made with (the scroll bars').
    QPalette header = ThemeStyle::palette(theme);
    // The headers and the proposal bar sit on the theme's background: its
    // text colors go with it.
    header.setColor(QPalette::PlaceholderText, theme.mutedText);
    setPalette(header);
    setAutoFillBackground(true);
    const QColor current = theme.isDark ? QColor("#6ea0ff") : QColor("#0969da");
    const QColor incoming = theme.isDark ? QColor("#f0b45a") : QColor("#9a6700");
    const QColor result = theme.isDark ? QColor("#d59cff") : QColor("#8250df");
    const QColor resultHeader = ThemeStyle::mixed(theme.window, result, 0.1);
    setStyleSheet(
        // Each header its background: inherited, it would keep the theme it was made with.
        QStringLiteral("QWidget#mergeCurrentHeader { background: %5; border-bottom: 2px solid %1; }"
                       "QWidget#mergeIncomingHeader { background: %5; border-bottom: 2px solid %2; }"
                       "QWidget#mergeResultHeader { background: %3; border-bottom: 2px solid %4; }"
                       "QWidget#mergeCurrentHeader QLabel#mergePaneTitle { color: %1; }"
                       "QWidget#mergeIncomingHeader QLabel#mergePaneTitle { color: %2; }"
                       "QWidget#mergeResultHeader QLabel#mergePaneTitle { color: %4; }")
            .arg(current.name(), incoming.name(), resultHeader.name(), result.name(), theme.window.name()));
    // The result's header and code reach the lines between the columns.
    constexpr int borderWidth = 2;
    columns_->setHandleSides(1, {.split = headerHeight_,
                                 .leftHeader = theme.window,
                                 .rightHeader = resultHeader,
                                 .leftBorder = current,
                                 .rightBorder = result,
                                 .borderWidth = borderWidth,
                                 .leftContent = theme.window,
                                 .rightContent = theme.background});
    columns_->setHandleSides(2, {.split = headerHeight_,
                                 .leftHeader = resultHeader,
                                 .rightHeader = theme.window,
                                 .leftBorder = result,
                                 .rightBorder = incoming,
                                 .borderWidth = borderWidth,
                                 .leftContent = theme.background,
                                 .rightContent = theme.window});
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
    nameLabel->setObjectName(QStringLiteral("mergePaneTitle"));
    auto *roleLabel = new QLabel(role, header);
    roleLabel->setObjectName(QStringLiteral("pageDescription"));
    // One line each, clipped in a narrow column: the headers stay level.
    for (QLabel *label : {nameLabel, roleLabel}) {
        label->setToolTip(label->text());
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    auto *labels = new QVBoxLayout;
    labels->setSpacing(4);
    labels->addWidget(nameLabel);
    labels->addWidget(roleLabel);
    layout->addLayout(labels, 1);
    return header;
}

QWidget *MergeTextPanel::createSideHeader(const QString &name, const QString &role, Choice side)
{
    QWidget *header = createHeader(name, role);
    header->setObjectName(side == Choice::Current ? QStringLiteral("mergeCurrentHeader")
                                                  : QStringLiteral("mergeIncomingHeader"));
    const QString text = side == Choice::Current ? tr("Take Yours") : tr("Take Incoming");
    auto *takeAll = new QPushButton(text, header);
    takeAll->setToolTip(tr("Take %1's lines for every conflict still open").arg(name));
    connect(takeAll, &QPushButton::clicked, this, [this, side] {
        document_.resolveRemaining(side);
        refresh();
    });
    header->layout()->addWidget(takeAll);
    takeAllButtons_.append(takeAll);
    return header;
}

// The conflict a third of the way down: what comes before it in view.
void MergeTextPanel::scrollTo(int segment)
{
    const int rowsOnScreen = int(result_->SendScintilla(QsciScintillaBase::SCI_LINESONSCREEN));
    result_->setFirstVisibleLine(std::max(0, segmentRows_[segment] - rowsOnScreen / 3));
}

void MergeTextPanel::showConflict(int segment)
{
    currentSegment_ = segment;
    scrollTo(segment);
    alignWith(*result_);
    updateMarks();
    focus_->flash();
    emit conflictsChanged();
}

void MergeTextPanel::moveOnFrom(int segment)
{
    const QList<int> conflicts = conflictSegments();
    if (!conflicts.contains(segment) || stateOf(segment) != ConflictState::Resolved)
        return;
    currentSegment_ = segment;
    const int next = nextOpenSegment(segment);
    emit conflictsChanged();
    emit conflictResolved(int(conflicts.indexOf(segment)), int(conflicts.indexOf(next)));
    if (next < 0)
        return;
    // The segments may have changed meanwhile (an undo, typing).
    QTimer::singleShot(moveOnDelayMs, this, [this, next] {
        if (conflictSegments().contains(next) && stateOf(next) != ConflictState::Resolved)
            showConflict(next);
    });
}

// After `after`, round to the start; -1 when none is open.
int MergeTextPanel::nextOpenSegment(int after) const
{
    const QList<int> conflicts = conflictSegments();
    const qsizetype start = conflicts.indexOf(after);
    for (qsizetype step = 1; step <= conflicts.size(); ++step) {
        const int segment = conflicts[(start + step + conflicts.size()) % conflicts.size()];
        if (stateOf(segment) != ConflictState::Resolved)
            return segment;
    }
    return -1;
}

ConflictState MergeTextPanel::stateOf(int segment) const
{
    if (document_.segments().at(segment).choice != Choice::Unresolved)
        return ConflictState::Resolved;
    return document_.proposalFor(segment) ? ConflictState::Proposed : ConflictState::Open;
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
    connect(overview_, &MergeOverview::conflictRequested, this, [this](int index) { goToConflict(index); });
    connect(overview_, &MergeOverview::rowRequested, this, [this](int row) {
        const int rowsOnScreen = int(result_->SendScintilla(QsciScintillaBase::SCI_LINESONSCREEN));
        result_->setFirstVisibleLine(std::max(0, row - rowsOnScreen / 2));
        alignWith(*result_);
    });
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
    add(QKeySequence(Qt::ALT | Qt::Key_F7), [this] { goToNextOpenConflict(); });
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
    const bool wasOpen = stateOf(segment) != ConflictState::Resolved;
    document_.choose(segment, addingSide(document_.segments().at(segment), taken));
    refresh();
    if (wasOpen)
        moveOnFrom(segment);
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
        const Shade shade = document_.proposalFor(i) ? Shade::Proposed : resultShade(segment);
        resultStretches.append({result, unlessUnchanged(segment, shade), rows});
        incomingStretches.append({segment.chunk.incoming, sideShade(segment, Side::Incoming), rows});
        segmentRows_.append(row);
        row += rows;
    }
    totalRows_ = row;
    for (QWidget *button : std::as_const(takeAllButtons_))
        button->setVisible(!document_.isResolved());
    // Typing joins segments: the conflict shown may be another one now.
    const QList<int> conflicts = conflictSegments();
    if (!conflicts.contains(currentSegment_)) {
        const int open = nextOpenSegment(-1);
        currentSegment_ = open >= 0 ? open : (conflicts.isEmpty() ? -1 : conflicts.first());
    }
    isShowing_ = true;
    current_->showStretches(currentStretches);
    result_->showStretches(resultStretches);
    incoming_->showStretches(incomingStretches);
    isShowing_ = false;
    result_->setFirstVisibleLine(top);
    alignWith(*result_);
    updateMarks();
    updateProposalBar();
    emit documentChanged();
    emit conflictsChanged();
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
    updateFocus();
    updateOverview();
}

void MergeTextPanel::updateFocus()
{
    if (!columns_ || segmentRows_.size() != document_.segments().size())
        return;
    const auto codeArea = [this](const MergePane *pane) {
        const QWidget *viewport = pane->viewport();
        return QRect(viewport->mapTo(this, QPoint(0, 0)) - focus_->pos(), viewport->size());
    };
    const QRect current = codeArea(current_);
    const QRect result = codeArea(result_);
    const QRect incoming = codeArea(incoming_);
    const int rowHeight = rowHeightOf(*result_);
    const int top = result_->firstVisibleLine();
    const QList<int> conflicts = conflictSegments();
    QList<MergeFocusFrame::Band> bands;
    for (qsizetype i = 0; i < conflicts.size(); ++i) {
        const int segment = conflicts[i];
        const int y = current.top() + (segmentRows_[segment] - top) * rowHeight;
        const int end = segment + 1 < segmentRows_.size() ? segmentRows_[segment + 1] : totalRows_;
        const int height = std::max(1, end - segmentRows_[segment]) * rowHeight;
        const ConflictState state = stateOf(segment);
        bands.append({.area = QRect(current.left(), y, incoming.right() - current.left() + 1, height),
                      .result = QRect(result.left(), y, result.width(), height),
                      .state = state,
                      .tag = tr("%1 / %2").arg(i + 1).arg(conflicts.size()),
                      .isCurrent = segment == currentSegment_});
    }
    focus_->setBands(bands, QRect(current.topLeft(), incoming.bottomRight()));
}

void MergeTextPanel::updateOverview()
{
    MergeOverview::Map map{.conflicts = {},
                           .current = currentConflict(),
                           .totalRows = totalRows_,
                           .firstVisibleRow = result_->firstVisibleLine(),
                           .visibleRows = int(result_->SendScintilla(QsciScintillaBase::SCI_LINESONSCREEN))};
    for (const int segment : conflictSegments()) {
        const int end = segment + 1 < segmentRows_.size() ? segmentRows_[segment + 1] : totalRows_;
        map.conflicts.append(
            {.firstRow = segmentRows_[segment], .rows = end - segmentRows_[segment], .state = stateOf(segment)});
    }
    overview_->setMap(map);
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
