#include "CompletionPopup.h"

#include "FuzzyMatch.h"

#include <QAbstractListModel>
#include <QListView>
#include <QPainter>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int maximumRows = 10;
constexpr int popupMinimumWidth = 300;
constexpr int popupMaximumWidth = 600;

struct Entry {
    CompletionItem item;
    FuzzyMatch::Match match;
};

// Letter and theme category of each kind's badge.
std::pair<QChar, const char *> badgeOf(CompletionItem::Kind kind)
{
    using Kind = CompletionItem::Kind;
    switch (kind) {
    case Kind::Keyword: return {QLatin1Char('k'), "keyword"};
    case Kind::Function: return {QLatin1Char('f'), "function"};
    case Kind::Method: return {QLatin1Char('m'), "function"};
    case Kind::Class: return {QLatin1Char('c'), "type"};
    case Kind::Struct: return {QLatin1Char('s'), "type"};
    case Kind::Interface: return {QLatin1Char('i'), "type"};
    case Kind::Enum: return {QLatin1Char('e'), "type"};
    case Kind::Namespace: return {QLatin1Char('n'), "keyword"};
    case Kind::Module: return {QLatin1Char('M'), "keyword"};
    case Kind::Type: return {QLatin1Char('t'), "type"};
    case Kind::Field: return {QLatin1Char('f'), "property"};
    case Kind::Property: return {QLatin1Char('p'), "property"};
    case Kind::Variable: return {QLatin1Char('v'), "variable"};
    case Kind::Constant: return {QLatin1Char('C'), "constant"};
    case Kind::Macro: return {QLatin1Char('#'), "keyword"};
    case Kind::Snippet: return {QLatin1Char('S'), "string"};
    case Kind::Text: break;
    }
    return {QLatin1Char('w'), "comment"};
}

} // namespace

class CompletionListModel : public QAbstractListModel {
public:
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(entries.size()); }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == Qt::DisplayRole && index.isValid())
            return entries[index.row()].item.label;
        return {};
    }
    void reset(std::vector<Entry> newEntries)
    {
        beginResetModel();
        entries = std::move(newEntries);
        endResetModel();
    }

    std::vector<Entry> entries;
};

namespace {

class CompletionDelegate : public QStyledItemDelegate {
public:
    CompletionDelegate(const CompletionListModel *model, const Theme *theme, QObject *parent)
        : QStyledItemDelegate(parent)
        , model_(model)
        , theme_(theme)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return {0, option.fontMetrics.height() + 8};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const Entry &entry = model_->entries[index.row()];
        const Theme &theme = *theme_;
        const bool selected = option.state & QStyle::State_Selected;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF row = QRectF(option.rect).adjusted(3, 1, -3, -1);
        if (selected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(theme.selection);
            painter->drawRoundedRect(row, 5, 5);
        }

        // The badge: the kind's letter on a tint of its syntax color.
        const auto [letter, category] = badgeOf(entry.item.kind);
        const int styleIndex = theme.syntaxStyleIndex(QString::fromLatin1(category));
        const QColor color = styleIndex >= 0 ? theme.syntaxStyles[styleIndex].second.foreground : theme.foreground;
        const qreal side = std::min<qreal>(row.height() - 6, 18);
        const QRectF badge(row.left() + 6, row.center().y() - side / 2, side, side);
        QColor tint = color;
        tint.setAlpha(theme.isDark ? 50 : 36);
        painter->setPen(Qt::NoPen);
        painter->setBrush(tint);
        painter->drawRoundedRect(badge, 4, 4);
        QFont badgeFont = option.font;
        badgeFont.setPointSizeF(badgeFont.pointSizeF() * 0.82);
        badgeFont.setWeight(QFont::Bold);
        painter->setFont(badgeFont);
        painter->setPen(color);
        painter->drawText(badge, Qt::AlignCenter, QString(letter));

        // The detail at the right, in the room the label leaves (the label
        // keeps at least half the row), then the label with the matched
        // letters.
        const QFontMetrics metrics(option.font);
        const qreal textLeft = badge.right() + 8;
        qreal detailWidth = 0;
        if (!entry.item.detail.isEmpty()) {
            QFont boldLabel = option.font;
            boldLabel.setWeight(QFont::Bold);
            const qreal labelWidth = std::min<qreal>(QFontMetrics(boldLabel).horizontalAdvance(entry.item.label),
                                                     (row.right() - textLeft) / 2);
            const int available = int(row.right() - textLeft - labelWidth - 28);
            const QString detail = metrics.elidedText(entry.item.detail, Qt::ElideMiddle, available);
            detailWidth = metrics.horizontalAdvance(detail) + 12;
            painter->setFont(option.font);
            painter->setPen(theme.mutedText);
            painter->drawText(QRectF(row.right() - detailWidth, row.top(), detailWidth - 8, row.height()),
                              Qt::AlignRight | Qt::AlignVCenter, detail);
        }

        QFont bold = option.font;
        bold.setWeight(QFont::Bold);
        const QFontMetrics boldMetrics(bold);
        const QString &label = entry.item.label;
        const qreal right = row.right() - detailWidth - 4;
        qreal x = textLeft;
        for (int i = 0; i < label.size(); ++i) {
            const bool matched = entry.match.positions.contains(i);
            const QFontMetrics &m = matched ? boldMetrics : metrics;
            const qreal width = m.horizontalAdvance(label[i]);
            if (x + width > right) {
                painter->setFont(option.font);
                painter->setPen(theme.mutedText);
                painter->drawText(QPointF(x, row.center().y() + metrics.ascent() / 2.0 - 1), QStringLiteral("…"));
                break;
            }
            painter->setFont(matched ? bold : option.font);
            painter->setPen(matched ? theme.accent : theme.foreground);
            painter->drawText(QPointF(x, row.center().y() + metrics.ascent() / 2.0 - 1), QString(label[i]));
            x += width;
        }
        painter->restore();
    }

private:
    const CompletionListModel *model_;
    const Theme *theme_;
};

} // namespace

CompletionPopup::CompletionPopup(QWidget *parent)
    : QFrame(parent)
    , view_(new QListView(this))
    , model_(new CompletionListModel(this))
{
    setObjectName(QStringLiteral("completionPopup"));
    setFocusPolicy(Qt::NoFocus);
    view_->setFocusPolicy(Qt::NoFocus);
    view_->setModel(model_);
    view_->setItemDelegate(new CompletionDelegate(model_, &theme_, view_));
    view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view_->setUniformItemSizes(true);
    view_->setFrameShape(QFrame::NoFrame);
    connect(view_, &QListView::doubleClicked, this, &CompletionPopup::activated);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(3, 3, 3, 3);
    layout->addWidget(view_);
    hide();
}

void CompletionPopup::applyTheme(const Theme &theme)
{
    theme_ = theme;
    view_->setFont(theme.font); // the editor's
    // Called each time the popup shows: a style sheet is only set when it
    // changes, setting one restyles the whole popup.
    const QString sheet =
        QStringLiteral("QFrame#completionPopup { background: %1; border: 1px solid %2; border-radius: 8px; }"
                       "QListView { background: transparent; border: none; outline: 0; }")
            .arg(theme.alternateBase.name(), theme.border.name());
    if (sheet != styleSheet())
        setStyleSheet(sheet);
    view_->viewport()->update();
}

int CompletionPopup::setItems(const QList<CompletionItem> &items, const QString &prefix)
{
    // Candidates are scored by index; only the kept ones are copied (an
    // item carries strings, edits and callbacks).
    struct Scored {
        int index;
        int score;
        FuzzyMatch::Match match;
    };
    std::vector<Scored> scored;
    for (int i = 0; i < items.size(); ++i) {
        const CompletionItem &item = items[i];
        // The word already typed in full is no suggestion.
        if (item.kind == CompletionItem::Kind::Text && item.label == prefix)
            continue;
        if (std::optional<FuzzyMatch::Match> match = FuzzyMatch::match(prefix, item.label))
            scored.push_back({i, match->score + item.priority, std::move(*match)});
    }
    const auto before = [&items](const Scored &a, const Scored &b) {
        if (a.score != b.score)
            return a.score > b.score;
        const CompletionItem &first = items[a.index];
        const CompletionItem &second = items[b.index];
        if (first.order != second.order)
            return first.order < second.order;
        if (first.label.size() != second.label.size())
            return first.label.size() < second.label.size();
        return a.index < b.index; // stable
    };
    constexpr size_t maxKept = 200; // nobody scrolls further
    const size_t kept = std::min(scored.size(), maxKept);
    std::partial_sort(scored.begin(), scored.begin() + static_cast<std::ptrdiff_t>(kept), scored.end(), before);
    std::vector<Entry> entries;
    entries.reserve(kept);
    for (size_t i = 0; i < kept; ++i)
        entries.push_back({items[scored[i].index], std::move(scored[i].match)});
    model_->reset(std::move(entries));
    if (model_->rowCount() > 0)
        setCurrentRow(0);
    if (isVisible())
        fitSize();
    return model_->rowCount();
}

int CompletionPopup::count() const
{
    return model_->rowCount();
}

const CompletionItem *CompletionPopup::current() const
{
    const int row = currentRow();
    return row >= 0 && row < count() ? &model_->entries[row].item : nullptr;
}

int CompletionPopup::currentRow() const
{
    return view_->currentIndex().row();
}

void CompletionPopup::setCurrentRow(int row)
{
    if (count() == 0)
        return;
    row = std::clamp(row, 0, count() - 1);
    view_->setCurrentIndex(model_->index(row));
    view_->scrollTo(model_->index(row));
}

void CompletionPopup::moveSelection(int rows)
{
    setCurrentRow(currentRow() + rows);
}

int CompletionPopup::pageRows() const
{
    return std::min(count(), maximumRows) - 1;
}

QStringList CompletionPopup::labels() const
{
    QStringList result;
    for (const Entry &entry : model_->entries)
        result.append(entry.item.label);
    return result;
}

void CompletionPopup::fitSize()
{
    const QFontMetrics metrics(view_->font());
    int width = popupMinimumWidth;
    for (int i = 0; i < std::min(count(), 60); ++i) {
        const CompletionItem &item = model_->entries[i].item;
        // Badge, bold label, gap, detail and margins (see CompletionDelegate).
        const int wanted = 56 + metrics.horizontalAdvance(item.label) * 11 / 10
                         + (item.detail.isEmpty() ? 0 : 40 + metrics.horizontalAdvance(item.detail));
        width = std::max(width, wanted);
    }
    // As CompletionDelegate::sizeHint, plus the frame's margins.
    const int rowHeight = metrics.height() + 8;
    const int rows = std::clamp(count(), 1, maximumRows);
    resize(std::min(width, popupMaximumWidth), rows * rowHeight + 8);
}

void CompletionPopup::showAt(const QPoint &anchor, int anchorTop)
{
    fitSize();
    const QWidget *area = parentWidget();
    QPoint position = anchor;
    if (position.y() + height() > area->height() && anchorTop - height() >= 0)
        position.setY(anchorTop - height()); // above the line
    position.setX(std::clamp(position.x(), 0, std::max(0, area->width() - width())));
    move(position);
    show();
    raise();
}
