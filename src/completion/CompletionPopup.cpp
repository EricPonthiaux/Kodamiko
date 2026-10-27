// SPDX-License-Identifier: GPL-3.0-only

#include "CompletionPopup.h"

#include "FuzzyMatch.h"
#include "ui/Motion.h"
#include "ui/Paragraph.h"
#include "ui/PopupMetrics.h"
#include "ui/PopupRows.h"
#include "ui/ThemeStyle.h"

#include <QAbstractListModel>
#include <QAccessible>
#include <QApplication>
#include <QFontMetrics>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int maximumRows = 10;
// In characters and lines of the code font, so that the popup grows with it.
constexpr int popupMinimumColumns = 34;
constexpr int popupMaximumColumns = 68;
constexpr int documentationColumns = 43;
constexpr int documentationMaximumLines = 17;
constexpr int resolveDelayMs = 150;
const QString documentationSetting = QStringLiteral("completion/showDocumentation");

struct Entry {
    CompletionItem item;
    FuzzyMatch::Match match;
    bool documentationRequested = false; // asked from the server once
};

// Letter and theme category of each kind's badge.
std::pair<QChar, const char *> badgeOf(CompletionItem::Kind kind)
{
    using Kind = CompletionItem::Kind;
    switch (kind) {
    case Kind::Keyword:
        return {QLatin1Char('k'), "keyword"};
    // Not "f", which fields use: the letter alone tells them apart.
    case Kind::Function:
        return {QChar(0x0192), "function"}; // ƒ
    case Kind::Method:
        return {QLatin1Char('m'), "function"};
    case Kind::Class:
        return {QLatin1Char('c'), "type"};
    case Kind::Struct:
        return {QLatin1Char('s'), "type"};
    case Kind::Interface:
        return {QLatin1Char('i'), "type"};
    case Kind::Enum:
        return {QLatin1Char('e'), "type"};
    case Kind::Namespace:
        return {QLatin1Char('n'), "keyword"};
    case Kind::Module:
        return {QLatin1Char('M'), "keyword"};
    case Kind::Type:
        return {QLatin1Char('t'), "type"};
    case Kind::Field:
        return {QLatin1Char('f'), "property"};
    case Kind::Property:
        return {QLatin1Char('p'), "property"};
    case Kind::Variable:
        return {QLatin1Char('v'), "variable"};
    case Kind::Constant:
        return {QLatin1Char('C'), "constant"};
    case Kind::Macro:
        return {QLatin1Char('#'), "keyword"};
    case Kind::Snippet:
        return {QLatin1Char('S'), "string"};
    case Kind::Text:
        break;
    }
    return {QLatin1Char('w'), "comment"};
}

} // namespace

QString kindName(CompletionItem::Kind kind)
{
    using Kind = CompletionItem::Kind;
    const auto tr = [](const char *text) { return QCoreApplication::translate("Completion", text); };
    switch (kind) {
    case Kind::Keyword:
        return tr("keyword");
    case Kind::Function:
        return tr("function");
    case Kind::Method:
        return tr("method");
    case Kind::Class:
        return tr("class");
    case Kind::Struct:
        return tr("struct");
    case Kind::Interface:
        return tr("interface");
    case Kind::Enum:
        return tr("enum");
    case Kind::Namespace:
        return tr("namespace");
    case Kind::Module:
        return tr("module");
    case Kind::Type:
        return tr("type");
    case Kind::Field:
        return tr("field");
    case Kind::Property:
        return tr("property");
    case Kind::Variable:
        return tr("variable");
    case Kind::Constant:
        return tr("constant");
    case Kind::Macro:
        return tr("macro");
    case Kind::Snippet:
        return tr("snippet");
    case Kind::Text:
        break;
    }
    return tr("word");
}

class CompletionListModel : public QAbstractListModel {
public:
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(entries.size()); }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid())
            return {};
        const CompletionItem &item = entries[index.row()].item;
        switch (role) {
        case Qt::DisplayRole:
            return item.label;
        // What a screen reader says of the row: the kind in words, not
        // only the badge's letter and color.
        case Qt::AccessibleTextRole:
            return QStringLiteral("%1, %2").arg(item.label, kindName(item.kind));
        case Qt::AccessibleDescriptionRole:
            return item.detail;
        case Qt::ToolTipRole:
            return kindName(item.kind);
        default:
            return {};
        }
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
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF row = QRectF(option.rect).adjusted(3, 1, -3, -1);
        PopupRows::paintBackground(painter, row, option.state, theme);

        // The badge: the kind's letter on a tint of its syntax color.
        const auto [letter, category] = badgeOf(entry.item.kind);
        const int styleIndex = theme.syntaxStyleIndex(QString::fromLatin1(category));
        const QColor color = styleIndex >= 0 ? theme.syntaxStyles[styleIndex].second.foreground : theme.foreground;
        const qreal side = std::min<qreal>(row.height() - 6, QFontMetricsF(option.font).height());
        const QRectF badge(row.left() + 6, row.center().y() - side / 2, side, side);
        QColor tint = color;
        tint.setAlpha(theme.isDark ? 50 : 36);
        painter->setPen(Qt::NoPen);
        painter->setBrush(tint);
        painter->drawRoundedRect(badge, 2, 2);
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

// A candidate scored by its index; only the kept ones are copied (an
// item carries strings, edits and callbacks).
struct ScoredItem {
    int index;
    int score;
    FuzzyMatch::Match match;
};

// The candidates matching `prefix`, unsorted.
std::vector<ScoredItem> scoredItems(const QList<CompletionItem> &items, const QString &prefix, bool anywhere)
{
    std::vector<ScoredItem> scored;
    for (int i = 0; i < items.size(); ++i) {
        const CompletionItem &item = items[i];
        // The word already typed in full is no suggestion.
        if (item.kind == CompletionItem::Kind::Text && item.label == prefix)
            continue;
        if (std::optional<FuzzyMatch::Match> match = FuzzyMatch::match(prefix, item.label, anywhere))
            scored.push_back({i, match->score + item.priority, std::move(*match)});
    }
    return scored;
}

// Higher scores first, then the source's order, then shorter labels.
bool ranksBefore(const QList<CompletionItem> &items, const ScoredItem &a, const ScoredItem &b)
{
    if (a.score != b.score)
        return a.score > b.score;
    const CompletionItem &first = items[a.index];
    const CompletionItem &second = items[b.index];
    if (first.order != second.order)
        return first.order < second.order;
    if (first.label.size() != second.label.size())
        return first.label.size() < second.label.size();
    return a.index < b.index; // stable
}

} // namespace

CompletionPopup::CompletionPopup(QWidget *parent)
    : QFrame(parent)
    , view_(new QListView(this))
    , model_(new CompletionListModel(this))
{
    setObjectName(QStringLiteral("completionPopup"));
    Motion::watchPopup(this);
    setFocusPolicy(Qt::NoFocus);
    view_->setFocusPolicy(Qt::NoFocus);
    view_->setModel(model_);
    view_->setItemDelegate(new CompletionDelegate(model_, &theme_, view_));
    view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view_->setUniformItemSizes(true);
    view_->setFrameShape(QFrame::NoFrame);
    view_->setMouseTracking(true); // the row under the mouse lights up
    connect(view_, &QListView::doubleClicked, this, &CompletionPopup::activated);
    connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] {
        updateDocumentation();
        emit currentChanged();
    });
    setAccessibleName(tr("Code completion"));
    view_->setAccessibleName(tr("Suggestions"));

    hint_ = new QLabel(this);
    hint_->setObjectName(QStringLiteral("completionHint"));
    hint_->setContentsMargins(8, 2, 8, 2);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(3, 3, 3, 3);
    layout->setSpacing(0);
    layout->addWidget(view_);
    layout->addWidget(hint_);

    documentation_ = new QFrame(parent);
    documentation_->setObjectName(QStringLiteral("completionDocumentation"));
    Motion::watchPopup(documentation_);
    documentation_->setFocusPolicy(Qt::NoFocus);
    documentation_->setAccessibleName(tr("Documentation"));
    documentationTitle_ = new QLabel(documentation_);
    documentationTitle_->setWordWrap(true);
    documentationTitle_->setTextFormat(Qt::PlainText);
    documentationBody_ = new QLabel(documentation_);
    documentationBody_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto *documentationLayout = new QVBoxLayout(documentation_);
    documentationLayout->setContentsMargins(12, 10, 12, 10);
    documentationLayout->setSpacing(8);
    documentationLayout->addWidget(documentationTitle_);
    documentationLayout->addWidget(documentationBody_, 1);
    documentation_->hide();
    documentationVisible_ = QSettings().value(documentationSetting, true).toBool();

    resolveTimer_.setSingleShot(true);
    resolveTimer_.setInterval(resolveDelayMs);
    connect(&resolveTimer_, &QTimer::timeout, this, &CompletionPopup::resolveDocumentation);
    hide();
}

CompletionPopup::~CompletionPopup()
{
    delete documentation_;
}

void CompletionPopup::applyTheme(const Theme &theme)
{
    theme_ = theme;
    view_->setFont(theme.font); // the editor's
    // Called each time the popup shows: a style sheet is only set when it
    // changes, setting one restyles the whole popup.
    const QString sheet =
        QStringLiteral("QFrame#completionPopup { background: %1; border: 1px solid %2; border-radius: %5px; }"
                       "QListView { background: transparent; border: none; outline: 0; }"
                       "QLabel#completionHint { color: %3; border-top: 1px solid %4; }")
            .arg(theme.alternateBase.name(), theme.border.name(), theme.mutedText.name(), theme.line.name())
            .arg(PopupMetrics::frameRadius);
    ThemeStyle::setStyleSheetIfChanged(this, sheet);
    const QString documentationSheet =
        QStringLiteral("QFrame#completionDocumentation { background: %1; border: 1px solid %2; border-radius: %4px; }"
                       "QLabel { color: %3; background: transparent; }")
            .arg(theme.alternateBase.name(), theme.border.name(), theme.foreground.name())
            .arg(PopupMetrics::frameRadius);
    ThemeStyle::setStyleSheetIfChanged(documentation_, documentationSheet);
    // The signature in the code font, the text in the interface's.
    documentationTitle_->setFont(theme.font);
    view_->viewport()->update();
}

int CompletionPopup::setItems(const QList<CompletionItem> &items, const QString &prefix, bool anywhere)
{
    std::vector<ScoredItem> scored = scoredItems(items, prefix, anywhere);
    const auto before = [&items](const ScoredItem &a, const ScoredItem &b) { return ranksBefore(items, a, b); };
    const size_t maxKept = anywhere ? 500 : 200; // nobody scrolls further
    const size_t kept = std::min(scored.size(), maxKept);
    std::partial_sort(scored.begin(), scored.begin() + static_cast<std::ptrdiff_t>(kept), scored.end(), before);
    std::vector<Entry> entries;
    entries.reserve(kept);
    for (size_t i = 0; i < kept; ++i)
        entries.push_back({items[scored[i].index], std::move(scored[i].match)});
    model_->reset(std::move(entries));
    if (model_->rowCount() > 0)
        setCurrentRow(0);
    updateDocumentation();
    if (isVisible())
        fitSize();
    return model_->rowCount();
}

void CompletionPopup::setKeyHint(const QString &hint)
{
    hint_->setText(hint);
    hint_->setAccessibleName(hint);
    hint_->setVisible(!hint.isEmpty());
}

QString CompletionPopup::keyHint() const
{
    return hint_->text();
}

QString CompletionPopup::describeCurrent() const
{
    const CompletionItem *item = current();
    if (!item)
        return {};
    QStringList parts{item->label, kindName(item->kind)};
    if (!item->detail.isEmpty())
        parts.append(item->detail);
    parts.append(tr("%1 of %2").arg(currentRow() + 1).arg(count()));
    return parts.join(QStringLiteral(", "));
}

void CompletionPopup::announce(const QString &message)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (message.isEmpty() || !QAccessible::isActive())
        return;
    // The editor keeps the focus: the screen reader listens to it.
    QObject *target = QApplication::focusWidget();
    if (!target)
        return;
    QAccessibleAnnouncementEvent event(target, message);
    QAccessible::updateAccessibility(&event);
#else
    Q_UNUSED(message);
#endif
}

void CompletionPopup::setDocumentationVisible(bool visible)
{
    if (visible == documentationVisible_)
        return;
    documentationVisible_ = visible;
    QSettings().setValue(documentationSetting, visible);
    updateDocumentation();
}

QString CompletionPopup::documentationText() const
{
    if (documentation_->isHidden())
        return {};
    return documentationTitle_->text() + QLatin1Char('\n') + documentation_->accessibleDescription();
}

// The kind and the signature, then the documentation, which a server may
// only send on demand.
void CompletionPopup::updateDocumentation()
{
    resolveTimer_.stop();
    const CompletionItem *item = current();
    if (!documentationVisible_ || !isVisible() || !item) {
        documentation_->hide();
        return;
    }
    const Entry &entry = model_->entries[size_t(currentRow())];
    QString title = item->label;
    if (!item->detail.isEmpty())
        title += QStringLiteral("  ") + item->detail;
    documentationTitle_->setText(title);
    QString body = item->documentation;
    if (body.isEmpty() && item->resolveDocumentation && !entry.documentationRequested) {
        body = tr("Loading the documentation…");
        resolveTimer_.start();
    }
    const QString text = body.isEmpty() ? kindName(item->kind) : kindName(item->kind) + QStringLiteral("\n\n") + body;
    Paragraph::setText(documentationBody_, text);
    documentation_->setAccessibleDescription(text);
    placeDocumentation();
    documentation_->show();
    documentation_->raise();
}

void CompletionPopup::resolveDocumentation()
{
    const int row = currentRow();
    if (row < 0 || row >= count())
        return;
    Entry &entry = model_->entries[size_t(row)];
    if (!entry.item.resolveDocumentation || entry.documentationRequested)
        return;
    entry.documentationRequested = true;
    const QString label = entry.item.label;
    const QPointer<CompletionPopup> self(this);
    entry.item.resolveDocumentation([self, row, label](const QString &text) {
        // The list may have changed meanwhile: only the same candidate.
        if (!self || row >= self->count() || self->model_->entries[size_t(row)].item.label != label)
            return;
        self->model_->entries[size_t(row)].item.documentation = text;
        if (self->currentRow() == row)
            self->updateDocumentation();
    });
}

// Right of the list, or left of it when there is no room.
void CompletionPopup::placeDocumentation()
{
    const QWidget *area = parentWidget();
    const QFontMetrics metrics(view_->font());
    const int width = std::min(documentationColumns * metrics.averageCharWidth(), std::max(area->width() / 3, 200));
    documentation_->setFixedWidth(width);
    const int height = std::min(documentation_->heightForWidth(width) > 0 ? documentation_->heightForWidth(width)
                                                                          : documentation_->sizeHint().height(),
                                documentationMaximumLines * metrics.height());
    documentation_->setFixedHeight(std::max(height, this->height() / 2));
    int x = geometry().right() + 4;
    if (x + width > area->width())
        x = geometry().left() - width - 4;
    documentation_->move(std::max(x, 0), y());
}

void CompletionPopup::hideEvent(QHideEvent *event)
{
    resolveTimer_.stop();
    documentation_->hide();
    QFrame::hideEvent(event);
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
    int width = popupMinimumColumns * metrics.averageCharWidth();
    for (int i = 0; i < std::min(count(), 60); ++i) {
        const CompletionItem &item = model_->entries[i].item;
        // Badge, bold label, gap, detail and margins (see CompletionDelegate).
        const int wanted = metrics.height() + 38 + metrics.horizontalAdvance(item.label) * 11 / 10 +
                           (item.detail.isEmpty() ? 0 : 40 + metrics.horizontalAdvance(item.detail));
        width = std::max(width, wanted);
    }
    // As CompletionDelegate::sizeHint, plus the frame's margins and the
    // line of keys.
    const int rowHeight = metrics.height() + 8;
    const int rows = std::clamp(count(), 1, maximumRows);
    const int hintHeight = hint_->isHidden() ? 0 : hint_->sizeHint().height();
    if (!hint_->isHidden())
        width = std::max(width, hint_->sizeHint().width() + 8); // the keys, in full
    resize(std::min(width, popupMaximumColumns * metrics.averageCharWidth()), rows * rowHeight + 8 + hintHeight);
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
    updateDocumentation();
}
