// SPDX-License-Identifier: GPL-3.0-only

#include "DiffFileStrip.h"

#include "DiffView.h"
#include "ui/ContextMenus.h"

#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>
#include <tuple>

namespace {

constexpr int marginPx = 6;
constexpr int gapPx = 6;
constexpr int paddingPx = 8;
// Between a chip's name and its counts.
constexpr int countGapPx = 6;
constexpr int maximumRows = 2;
constexpr int minimumFileCount = 2;

QColor syntaxColor(const Theme &theme, QStringView capture, const QColor &fallback)
{
    const int index = theme.syntaxStyleIndex(capture);
    return index >= 0 ? theme.syntaxStyles.at(index).second.foreground : fallback;
}

QString fileName(const QString &path)
{
    return path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
}

// A chip's width: its name, then "+a" and "−r" when not zero.
int chipWidth(const QFontMetrics &metrics, const QString &name, const DiffView::FileSummary &file)
{
    int width = 2 * paddingPx + metrics.horizontalAdvance(name);
    if (file.added > 0)
        width += countGapPx + metrics.horizontalAdvance(QStringLiteral("+%1").arg(file.added));
    if (file.removed > 0)
        width += countGapPx + metrics.horizontalAdvance(QStringLiteral("−%1").arg(file.removed));
    return width;
}

} // namespace

DiffFileStrip::DiffFileStrip(DiffView *diff, QWidget *parent)
    : QWidget(parent)
    , diff_(diff)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    connect(diff_, &DiffView::contentChanged, this, &DiffFileStrip::onContentChanged);
    // Scrolled, or lines revealed: as the diff's header follows it.
    connect(diff_, &ScintillaEditBase::updateUi, this, &DiffFileStrip::followScroll);
    onContentChanged();
}

void DiffFileStrip::applyTheme(const Theme &theme)
{
    background_ = theme.window;
    chipBackground_ = theme.button;
    currentBackground_ = theme.accent;
    currentBackground_.setAlphaF(0.22F);
    text_ = theme.foreground;
    mutedText_ = theme.mutedText;
    added_ = syntaxColor(theme, u"diff.plus", theme.accent);
    removed_ = syntaxColor(theme, u"diff.minus", theme.error);
    rule_ = theme.line;
    update();
}

QRect DiffFileStrip::chipRect(int file) const
{
    const auto chip = std::ranges::find_if(chips_, [file](const Chip &each) { return each.file == file; });
    return chip != chips_.end() ? chip->rect : QRect();
}

int DiffFileStrip::heightForWidth(int width) const
{
    const QList<Chip> chips = layOutChips(width);
    if (chips.isEmpty())
        return 0;
    const int bottom = std::ranges::max(chips, {}, [](const Chip &chip) { return chip.rect.bottom(); }).rect.bottom();
    return bottom + 1 + marginPx;
}

QSize DiffFileStrip::sizeHint() const
{
    return {width(), heightForWidth(width())};
}

QString DiffFileStrip::chipText(int file) const
{
    const QList<DiffView::FileSummary> &files = diff_->fileSummaries();
    const DiffView::FileSummary &shown = files.at(file);
    // A file in two parts (staged, unstaged): each says which.
    const auto isSameFile = [&shown](const DiffView::FileSummary &other) { return other.path == shown.path; };
    const bool hasTwoParts = std::ranges::count_if(files, isSameFile) > 1;
    if (!hasTwoParts || shown.status.isEmpty())
        return fileName(shown.path);
    return QStringLiteral("%1 · %2").arg(fileName(shown.path), shown.status.simplified());
}

QString DiffFileStrip::moreText(int count) const
{
    return tr("+%n more", nullptr, count);
}

QList<DiffFileStrip::Chip> DiffFileStrip::layOutChips(int width) const
{
    const QList<DiffView::FileSummary> &files = diff_->fileSummaries();
    QList<Chip> chips;
    if (files.size() < minimumFileCount)
        return chips;
    const QFontMetrics metrics(font());
    const int height = metrics.height() + 6;
    const int right = width - marginPx;
    int x = marginPx;
    int row = 0;
    for (int file = 0; file < files.size(); ++file) {
        const int chipWidthPx = std::min(chipWidth(metrics, chipText(file), files.at(file)), right - marginPx);
        if (x > marginPx && x + chipWidthPx > right) {
            ++row;
            x = marginPx;
        }
        if (row == maximumRows)
            break;
        chips.append({file, QRect(x, marginPx + row * (height + gapPx), chipWidthPx, height)});
        x += chipWidthPx + gapPx;
    }
    if (chips.size() == files.size())
        return chips;
    // Room for "+N more" after the last chip kept.
    const auto moreWidth = [&] {
        return 2 * paddingPx + metrics.horizontalAdvance(moreText(int(files.size() - chips.size())));
    };
    while (chips.size() > 1 && chips.last().rect.right() + 1 + gapPx + moreWidth() > right)
        chips.removeLast();
    const QRect last = chips.last().rect;
    chips.append({-1, QRect(last.right() + 1 + gapPx, last.top(), moreWidth(), height)});
    return chips;
}

void DiffFileStrip::onContentChanged()
{
    setVisible(diff_->fileSummaries().size() >= minimumFileCount);
    chips_ = layOutChips(width());
    current_ = -1;
    updateGeometry();
    followScroll();
    update();
}

void DiffFileStrip::followScroll()
{
    const int top = int(diff_->send(SCI_DOCLINEFROMVISIBLE, diff_->firstVisibleLine()));
    const int file = diff_->fileSummaries().isEmpty() ? -1 : std::max(0, diff_->locate(top).file);
    if (file == current_)
        return;
    current_ = file;
    update();
}

void DiffFileStrip::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    chips_ = layOutChips(width());
    // Narrower, the chips may take another row.
    if (heightForWidth(width()) != height())
        updateGeometry();
}

const DiffFileStrip::Chip *DiffFileStrip::chipAt(QPoint position) const
{
    const auto chip =
        std::ranges::find_if(chips_, [position](const Chip &each) { return each.rect.contains(position); });
    return chip != chips_.end() ? &*chip : nullptr;
}

void DiffFileStrip::scrollToFile(int file)
{
    diff_->scrollToTop(diff_->fileStart(file));
    followScroll();
}

void DiffFileStrip::showMoreMenu(const QRect &chip, int first)
{
    const QList<DiffView::FileSummary> &files = diff_->fileSummaries();
    QMenu menu;
    for (int file = first; file < files.size(); ++file) {
        QAction *action = menu.addAction(files.at(file).path, this, [this, file] { scrollToFile(file); });
        action->setCheckable(file == current_);
        action->setChecked(file == current_);
    }
    ContextMenus::exec(menu, *this, mapToGlobal(chip.bottomLeft()));
}

void DiffFileStrip::mousePressEvent(QMouseEvent *event)
{
    const Chip *chip = chipAt(event->position().toPoint());
    if (!chip || event->button() != Qt::LeftButton)
        return;
    if (chip->file >= 0) {
        scrollToFile(chip->file);
        return;
    }
    // The chip before "+N more" is the last file shown.
    const int first = chips_.size() > 1 ? chips_.at(chips_.size() - 2).file + 1 : 0;
    showMoreMenu(chip->rect, first);
}

void DiffFileStrip::mouseDoubleClickEvent(QMouseEvent *event)
{
    const Chip *chip = chipAt(event->position().toPoint());
    if (chip && chip->file >= 0 && event->button() == Qt::LeftButton)
        emit fileActivated(diff_->fileSummaries().at(chip->file).path);
}

bool DiffFileStrip::event(QEvent *event)
{
    if (event->type() != QEvent::ToolTip)
        return QWidget::event(event);
    auto *help = static_cast<QHelpEvent *>(event);
    const Chip *chip = chipAt(help->pos());
    if (!chip) {
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    const QString text =
        chip->file >= 0
            ? tr("%1\nDouble-click: open the file in the editor").arg(diff_->fileSummaries().at(chip->file).path)
            : tr("The other files of the diff");
    QToolTip::showText(help->globalPos(), text, this, chip->rect);
    return true;
}

void DiffFileStrip::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), background_);
    painter.setPen(QPen(rule_, 1));
    painter.drawLine(QPointF(0, height() - 0.5), QPointF(width(), height() - 0.5));
    painter.setRenderHint(QPainter::Antialiasing);
    const int shownFiles = int(std::ranges::count_if(chips_, [](const Chip &chip) { return chip.file >= 0; }));
    const int hiddenCount = int(diff_->fileSummaries().size()) - shownFiles;
    for (const Chip &chip : std::as_const(chips_))
        paintChip(painter, chip, hiddenCount);
}

void DiffFileStrip::paintChip(QPainter &painter, const Chip &chip, int hiddenCount) const
{
    const bool isCurrent = chip.file >= 0 && chip.file == current_;
    painter.setPen(Qt::NoPen);
    painter.setBrush(isCurrent ? currentBackground_ : chipBackground_);
    painter.drawRoundedRect(QRectF(chip.rect), 5, 5);
    const QRect text = chip.rect.adjusted(paddingPx, 0, -paddingPx, 0);
    painter.setFont(font());
    if (chip.file < 0) {
        painter.setPen(mutedText_);
        painter.drawText(text, Qt::AlignCenter, moreText(hiddenCount));
        return;
    }
    const DiffView::FileSummary &file = diff_->fileSummaries().at(chip.file);
    const QFontMetrics metrics(font());
    // The counts at the right, the name elided in what is left.
    int right = text.right() + 1;
    for (const auto &[count, sign, color] : {std::tuple{file.removed, u'−', removed_}, {file.added, u'+', added_}}) {
        if (count <= 0)
            continue;
        const QString shown = QChar(sign) + QString::number(count);
        const int shownWidth = metrics.horizontalAdvance(shown);
        right -= shownWidth;
        painter.setPen(color);
        painter.drawText(QRect(right, text.top(), shownWidth, text.height()), Qt::AlignVCenter, shown);
        right -= countGapPx;
    }
    painter.setPen(isCurrent ? text_ : mutedText_);
    const int nameWidth = right - text.left();
    painter.drawText(QRect(text.left(), text.top(), nameWidth, text.height()), Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(chipText(chip.file), Qt::ElideRight, nameWidth));
}
