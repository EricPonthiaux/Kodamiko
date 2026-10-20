// DiffView: the two modes, and the pane of the old version side by side.

#include "DiffView.h"
#include "SideBySide.h"

#include <QResizeEvent>

namespace {

// `follower` scrolled as `leader` is, vertically and horizontally.
void alignScroll(DiffView &follower, const DiffView &leader)
{
    if (follower.firstVisibleLine() != leader.firstVisibleLine())
        follower.setFirstVisibleLine(leader.firstVisibleLine());
    const long offset = leader.SendScintilla(QsciScintillaBase::SCI_GETXOFFSET);
    if (follower.SendScintilla(QsciScintillaBase::SCI_GETXOFFSET) != offset)
        follower.SendScintilla(QsciScintillaBase::SCI_SETXOFFSET, offset);
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
    oldSide_->setFrameShape(QFrame::NoFrame);
    oldSide_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    oldSide_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Whichever side scrolls, by the wheel or the keys, the other follows.
    connect(this, &QsciScintillaBase::SCN_UPDATEUI, oldSide_, [this] { alignScroll(*oldSide_, *this); });
    connect(oldSide_, &QsciScintillaBase::SCN_UPDATEUI, this, [this] { alignScroll(*this, *oldSide_); });
}

void DiffView::placeOldSide()
{
    const bool isSplit = mode_ == Mode::SideBySide && !files_.isEmpty();
    const int width = isSplit ? contentsRect().width() / 2 : 0;
    if (viewportMargins().left() != width)
        setViewportMargins(width, 0, 0, 0);
    if (!oldSide_)
        return;
    oldSide_->setVisible(isSplit);
    // A pixel short: the view's background between the two, as a rule.
    const QRect contents = contentsRect();
    oldSide_->setGeometry(contents.x(), contents.y(), std::max(0, width - 1), viewport()->height());
}

void DiffView::resizeEvent(QResizeEvent *event)
{
    QsciScintilla::resizeEvent(event);
    placeOldSide();
}
