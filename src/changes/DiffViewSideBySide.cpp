// SPDX-License-Identifier: GPL-3.0-only

// DiffView: the two modes, and the pane of the old version side by side.

#include "DiffView.h"
#include "SideBySide.h"
#include "SideBySideDivider.h"

#include <QResizeEvent>

#include <algorithm>

namespace {

// Neither side narrower, whatever the divider is dragged to.
constexpr int minSideWidth = 80;
constexpr int dividerWidth = 7;

// The old side's width out of the view's `width`: `wanted`, leaving each
// side its minimum; halves when the two minimums do not fit.
int oldSideWidth(int wanted, int width)
{
    if (width < 2 * minSideWidth)
        return width / 2;
    return std::clamp(wanted, minSideWidth, width - minSideWidth);
}

// `follower` scrolled as `leader` is, vertically and horizontally.
void alignScroll(DiffView &follower, const DiffView &leader)
{
    if (follower.firstVisibleLine() != leader.firstVisibleLine())
        follower.setFirstVisibleLine(leader.firstVisibleLine());
    const long offset = leader.SendScintilla(QsciScintillaBase::SCI_GETXOFFSET);
    if (follower.SendScintilla(QsciScintillaBase::SCI_GETXOFFSET) != offset)
        follower.SendScintilla(QsciScintillaBase::SCI_SETXOFFSET, offset);
}

// `follower` zoomed as `leader` is: the rows keep facing each other.
void alignZoom(DiffView &follower, const DiffView &leader)
{
    const int zoom = int(leader.SendScintilla(QsciScintillaBase::SCI_GETZOOM));
    if (follower.SendScintilla(QsciScintillaBase::SCI_GETZOOM) != zoom)
        follower.zoomTo(zoom);
}

} // namespace

void DiffView::setMode(Mode mode)
{
    if (mode == mode_)
        return;
    mode_ = mode;
    if (mode == Mode::SideBySide && !oldSide_)
        createOldSide();
    numbers_ = mode == Mode::SideBySide ? NumberColumns::NewOnly : NumberColumns::Both;
    // The rows differ between the modes: near the same place only.
    const int top = firstVisibleLine();
    layOut();
    setFirstVisibleLine(top);
}

QList<DiffLocation> DiffView::locations(int line) const
{
    QList<DiffLocation> result{locate(line)};
    if (mode_ == Mode::SideBySide)
        result.append(oldSide_->locate(line));
    return result;
}

void DiffView::layOut()
{
    QList<DiffLine> lines = DiffLayout::lines(files_, revealed_);
    if (mode_ == Mode::SideBySide) {
        DiffColumns columns = SideBySide::columns(lines);
        oldSide_->files_ = files_; // for the colors of each file's language
        oldSide_->showLines(std::move(columns.oldSide));
        lines = std::move(columns.newSide);
    }
    showLines(std::move(lines));
    placeOldSide();
    emit contentChanged();
}

void DiffView::createOldSide()
{
    oldSide_ = new DiffView(theme_, this);
    oldSide_->numbers_ = NumberColumns::OldOnly;
    // The keys act on the new side, which keeps the focus.
    oldSide_->setFocusPolicy(Qt::NoFocus);
    // Blank space facing the notes of the new side, without their boxes.
    oldSide_->SendScintilla(SCI_ANNOTATIONSETVISIBLE, long(ANNOTATION_STANDARD));
    oldSide_->setFrameShape(QFrame::NoFrame);
    oldSide_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    oldSide_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Whichever side scrolls, by the wheel or the keys, the other follows.
    connect(this, &QsciScintillaBase::SCN_UPDATEUI, oldSide_, [this] { alignScroll(*oldSide_, *this); });
    connect(oldSide_, &QsciScintillaBase::SCN_UPDATEUI, this, [this] { alignScroll(*this, *oldSide_); });
    // Same for the zoom (Ctrl+wheel), from the start.
    alignZoom(*oldSide_, *this);
    connect(this, &QsciScintillaBase::SCN_ZOOM, oldSide_, [this] { alignZoom(*oldSide_, *this); });
    connect(oldSide_, &QsciScintillaBase::SCN_ZOOM, this, [this] { alignZoom(*this, *oldSide_); });
    divider_ = new SideBySideDivider(this);
    // Only reachable shares are kept: dragged past a minimum, the side
    // still grows with the view.
    connect(divider_, &SideBySideDivider::dragged, this, [this](int x) {
        const QRect contents = contentsRect();
        oldSideShare_ = qreal(oldSideWidth(x - contents.x(), contents.width())) / std::max(1, contents.width());
        placeOldSide();
    });
}

void DiffView::placeOldSide()
{
    const bool isSplit = mode_ == Mode::SideBySide && !files_.isEmpty();
    const QRect contents = contentsRect();
    const int width = isSplit ? oldSideWidth(int(contents.width() * oldSideShare_), contents.width()) : 0;
    if (viewportMargins().left() != width)
        setViewportMargins(width, 0, 0, 0);
    if (!oldSide_)
        return;
    oldSide_->setVisible(isSplit);
    divider_->setVisible(isSplit);
    // A pixel short: the view's background between the two, as a rule.
    oldSide_->setGeometry(contents.x(), contents.y(), std::max(0, width - 1), viewport()->height());
    // Centered on that rule, over the two sides.
    divider_->setGeometry(contents.x() + width - 1 - dividerWidth / 2, contents.y(), dividerWidth,
                          viewport()->height());
    divider_->raise();
}

void DiffView::resizeEvent(QResizeEvent *event)
{
    QsciScintilla::resizeEvent(event);
    placeOldSide();
}
