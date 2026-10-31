// SPDX-License-Identifier: GPL-3.0-only

#include "ChangeMap.h"

#include "DiffView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

#include <algorithm>

namespace {

constexpr int widthPx = 14;
constexpr int insetPx = 3;
// A run of one line still shows.
constexpr qreal minimumRunPx = 2;

QColor syntaxColor(const Theme &theme, QStringView capture, const QColor &fallback)
{
    const int index = theme.syntaxStyleIndex(capture);
    return index >= 0 ? theme.syntaxStyles.at(index).second.foreground : fallback;
}

} // namespace

ChangeMap::ChangeMap(DiffView *diff, QWidget *parent)
    : QWidget(parent)
    , diff_(diff)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Every change of the diff: click to go there"));
    const auto repaint = [this] { update(); };
    connect(diff_, &DiffView::contentChanged, this, repaint);
    // Scrolled, or lines revealed.
    connect(diff_->verticalScrollBar(), &QScrollBar::valueChanged, this, repaint);
    connect(diff_->verticalScrollBar(), &QScrollBar::rangeChanged, this, repaint);
}

void ChangeMap::applyTheme(const Theme &theme)
{
    background_ = theme.window;
    added_ = syntaxColor(theme, u"diff.plus", theme.accent);
    removed_ = syntaxColor(theme, u"diff.minus", theme.error);
    rule_ = theme.border;
    frame_ = theme.mutedText;
    update();
}

QSize ChangeMap::sizeHint() const
{
    return {widthPx, 100};
}

qreal ChangeMap::scale() const
{
    const qsizetype lineCount = diff_->displayedLines().size();
    return lineCount > 0 ? qreal(height()) / qreal(lineCount) : 0;
}

std::pair<int, int> ChangeMap::linesInView() const
{
    // Notes under lines take display lines of their own: in lines of the text.
    const auto lineOf = [this](long displayLine) { return int(diff_->send(SCI_DOCLINEFROMVISIBLE, displayLine)); };
    const long top = diff_->firstVisibleLine();
    const int first = lineOf(top);
    const int last = lineOf(top + diff_->send(SCI_LINESONSCREEN));
    return {first, std::max(1, last - first)};
}

void ChangeMap::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), background_);
    painter.setPen(QPen(rule_, 1));
    painter.drawLine(QPointF(0.5, 0), QPointF(0.5, height()));
    if (diff_->displayedLines().isEmpty())
        return;
    paintChanges(painter);
    const auto [first, count] = linesInView();
    const QRectF frame(1.5, first * scale() + 0.5, width() - 2.0, std::max(4.0, count * scale() - 1));
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(frame_, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(frame, 2, 2);
}

// Each run of added or removed lines as one bar; a rule at each file.
void ChangeMap::paintChanges(QPainter &painter) const
{
    const QList<DiffLine> &lines = diff_->displayedLines();
    const qreal perLine = scale();
    const QRectF column(insetPx, 0, width() - 2 * insetPx, height());
    qsizetype start = 0;
    while (start < lines.size()) {
        const DiffLineKind kind = lines.at(start).kind;
        qsizetype end = start + 1;
        while (end < lines.size() && lines.at(end).kind == kind)
            ++end;
        const qreal top = start * perLine;
        if (kind == DiffLineKind::FileHeader && start > 1) {
            painter.fillRect(QRectF(0, top, width(), 1), rule_);
        } else if (kind == DiffLineKind::Added || kind == DiffLineKind::Removed) {
            const QColor &color = kind == DiffLineKind::Added ? added_ : removed_;
            painter.fillRect(
                QRectF(column.left(), top, column.width(), std::max(minimumRunPx, (end - start) * perLine)), color);
        }
        start = end;
    }
}

void ChangeMap::mousePressEvent(QMouseEvent *event)
{
    scrollTo(event->position().y());
}

void ChangeMap::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
        scrollTo(event->position().y());
}

void ChangeMap::scrollTo(qreal y)
{
    if (scale() <= 0)
        return;
    const int line = int(y / scale());
    const int shown = linesInView().second;
    diff_->scrollToTop(std::max(0, line - shown / 2));
}
