#include "OutlineView.h"

#include "IOutlineProvider.h"
#include "OutlineProviders.h"
#include "editor/EditorWidget.h"
#include "ui/TreeColumnFit.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <functional>

namespace {

constexpr int refreshDelayMs = 300;

enum ItemRole {
    PositionRole = Qt::UserRole,
    StartLineRole,
    EndLineRole
};

struct KindStyle {
    char letter;
    QColor color;
    const char *description;
};

KindStyle styleOf(SymbolKind kind)
{
    switch (kind) {
    case SymbolKind::Namespace:
        return {'N', QColor("#8a94a6"), QT_TRANSLATE_NOOP("OutlineView", "Namespace")};
    case SymbolKind::Class:
        return {'C', QColor("#d19a40"), QT_TRANSLATE_NOOP("OutlineView", "Class")};
    case SymbolKind::Struct:
        return {'S', QColor("#c7783e"), QT_TRANSLATE_NOOP("OutlineView", "Struct")};
    case SymbolKind::Interface:
        return {'I', QColor("#3fa7a0"), QT_TRANSLATE_NOOP("OutlineView", "Interface")};
    case SymbolKind::Enum:
        return {'E', QColor("#b5a13a"), QT_TRANSLATE_NOOP("OutlineView", "Enum")};
    case SymbolKind::Type:
        return {'T', QColor("#4fa36b"), QT_TRANSLATE_NOOP("OutlineView", "Type")};
    case SymbolKind::Function:
        return {'f', QColor("#9b6fd1"), QT_TRANSLATE_NOOP("OutlineView", "Function")};
    case SymbolKind::Method:
        return {'m', QColor("#a573c9"), QT_TRANSLATE_NOOP("OutlineView", "Method")};
    case SymbolKind::Field:
        return {'v', QColor("#4b8fd6"), QT_TRANSLATE_NOOP("OutlineView", "Field")};
    case SymbolKind::Macro:
        return {'#', QColor("#c25b6b"), QT_TRANSLATE_NOOP("OutlineView", "Macro")};
    case SymbolKind::Heading:
        return {'H', QColor("#4b8fd6"), QT_TRANSLATE_NOOP("OutlineView", "Heading")};
    case SymbolKind::Target:
        return {'T', QColor("#4fa36b"), QT_TRANSLATE_NOOP("OutlineView", "Target")};
    }
    return {'?', QColor("#8a94a6"), ""};
}

// Colored badge bearing a letter, readable on light and dark backgrounds alike.
QIcon iconFor(SymbolKind kind)
{
    static QHash<int, QIcon> cache;
    if (auto it = cache.constFind(static_cast<int>(kind)); it != cache.constEnd())
        return *it;

    const KindStyle style = styleOf(kind);
    QIcon icon;
    for (int size : {16, 32}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(style.color);
        const qreal margin = size / 16.0;
        painter.drawRoundedRect(QRectF(margin, margin, size - 2 * margin, size - 2 * margin), size / 4.0, size / 4.0);
        QFont font = painter.font();
        font.setPixelSize(size * 11 / 16);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, QString(QLatin1Char(style.letter)));
        icon.addPixmap(pixmap);
    }
    cache.insert(static_cast<int>(kind), icon);
    return icon;
}

} // namespace

OutlineView::OutlineView(QWidget *parent)
    : QWidget(parent)
    , filter_(new QLineEdit(this))
    , tree_(new QTreeWidget(this))
    , placeholder_(new QLabel(this))
    , stack_(new QStackedWidget(this))
{
    filter_->setPlaceholderText(tr("Filter symbols"));
    filter_->setClearButtonEnabled(true);
    filter_->installEventFilter(this);
    connect(filter_, &QLineEdit::textChanged, this, [this] {
        applyFilter();
        selectSymbolAtCursor();
    });
    connect(filter_, &QLineEdit::returnPressed, this, [this] { jumpTo(firstMatchingItem()); });

    tree_->header()->hide();
    tree_->setUniformRowHeights(true);
    columnFit_ = new TreeColumnFit(tree_);
    tree_->setAnimated(false);
    tree_->setExpandsOnDoubleClick(false);
    connect(tree_, &QTreeWidget::itemClicked, this, &OutlineView::jumpTo);
    connect(tree_, &QTreeWidget::itemActivated, this, &OutlineView::jumpTo);
    connect(tree_, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
        if (editor_ && filter_->text().trimmed().isEmpty())
            collapsedPaths_[editor_->filePath()].insert(pathOf(item));
    });
    connect(tree_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        if (editor_ && filter_->text().trimmed().isEmpty())
            collapsedPaths_[editor_->filePath()].remove(pathOf(item));
    });

    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setWordWrap(true);
    placeholder_->setEnabled(false);

    stack_->addWidget(placeholder_);
    stack_->addWidget(tree_);

    // Same margins as the scope box above the file tree.
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(4, 2, 2, 2);
    toolbar->addWidget(filter_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(toolbar);
    layout->addWidget(stack_, 1);

    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(refreshDelayMs);
    connect(&refreshTimer_, &QTimer::timeout, this, &OutlineView::refresh);

    refresh();
}

OutlineView::~OutlineView() = default;

void OutlineView::setEditor(EditorWidget *editor)
{
    if (editor == editor_)
        return;
    for (const QMetaObject::Connection &connection : std::as_const(editorConnections_))
        disconnect(connection);
    editorConnections_.clear();

    editor_ = editor;
    if (editor_) {
        editorConnections_ = {
            connect(editor_, &QsciScintilla::textChanged, this, &OutlineView::scheduleRefresh),
            connect(editor_, &QsciScintilla::cursorPositionChanged, this, &OutlineView::selectSymbolAtCursor),
            connect(editor_, &EditorWidget::languageChanged, this,
                    [this] {
                        updateProvider();
                        refresh();
                    }),
        };
    }
    updateProvider();
    refresh();
}

void OutlineView::focusFilter()
{
    filter_->setFocus();
    filter_->selectAll();
}

void OutlineView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (needsRefresh_)
        refresh();
}

// From the filter: Down moves into the list, Escape returns to the editor.
bool OutlineView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == filter_ && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Down && stack_->currentWidget() == tree_) {
            tree_->setFocus();
            if (!tree_->currentItem() || tree_->currentItem()->isHidden())
                tree_->setCurrentItem(firstMatchingItem());
            return true;
        }
        if (key == Qt::Key_Escape) {
            filter_->clear();
            if (editor_)
                editor_->setFocus();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void OutlineView::updateProvider()
{
    const QString language = editor_ ? editor_->languageId() : QString();
    if (language == providerLanguage_ && (provider_ || language.isEmpty()))
        return;
    providerLanguage_ = language;
    provider_ = language.isEmpty() ? nullptr : OutlineProviders::create(language);
}

void OutlineView::scheduleRefresh()
{
    refreshTimer_.start();
}

void OutlineView::refresh()
{
    refreshTimer_.stop();
    if (!isVisible()) {
        needsRefresh_ = true;
        return;
    }
    needsRefresh_ = false;

    const auto showPlaceholder = [this](const QString &message) {
        tree_->clear();
        placeholder_->setText(message);
        stack_->setCurrentWidget(placeholder_);
    };
    if (!editor_) {
        showPlaceholder(tr("No open file"));
        return;
    }
    if (!provider_) {
        showPlaceholder(tr("No outline for this file type"));
        return;
    }

    const auto *text =
        static_cast<const char *>(editor_->SendScintillaPtrResult(QsciScintillaBase::SCI_GETCHARACTERPOINTER));
    const long length = editor_->SendScintilla(QsciScintillaBase::SCI_GETLENGTH);
    const std::vector<OutlineSymbol> symbols = provider_->symbols(QByteArray(text, length));
    if (symbols.empty()) {
        showPlaceholder(tr("No symbols in this file"));
        return;
    }
    populate(symbols);
    stack_->setCurrentWidget(tree_);
    selectSymbolAtCursor();
}

void OutlineView::populate(const std::vector<OutlineSymbol> &symbols)
{
    const QSignalBlocker blocker(tree_);
    const int scroll = tree_->verticalScrollBar()->value();
    tree_->clear();

    const std::function<void(QTreeWidgetItem *, const std::vector<OutlineSymbol> &)> add =
        [&](QTreeWidgetItem *parent, const std::vector<OutlineSymbol> &children) {
            for (const OutlineSymbol &symbol : children) {
                auto *item = new QTreeWidgetItem(parent);
                item->setText(0, symbol.name);
                item->setIcon(0, iconFor(symbol.kind));
                item->setToolTip(0, QStringLiteral("%1 — %2 (line %3)")
                                        .arg(symbol.name, tr(styleOf(symbol.kind).description))
                                        .arg(symbol.startLine + 1));
                item->setData(0, PositionRole, symbol.position);
                item->setData(0, StartLineRole, symbol.startLine);
                item->setData(0, EndLineRole, symbol.endLine);
                add(item, symbol.children);
            }
        };
    add(tree_->invisibleRootItem(), symbols);

    applyFilter();
    tree_->verticalScrollBar()->setValue(scroll);
}

// Without a filter: everything is visible, expanded except what the user collapsed.
// With a filter: only matching symbols and their parents remain.
void OutlineView::applyFilter()
{
    const QSignalBlocker blocker(tree_);
    const QString text = filter_->text().trimmed();
    const QSet<QString> collapsed = editor_ ? collapsedPaths_.value(editor_->filePath()) : QSet<QString>();

    const std::function<bool(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
        bool childVisible = false;
        for (int i = 0; i < item->childCount(); ++i)
            childVisible |= visit(item->child(i));
        const bool matches = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive);
        item->setHidden(!matches && !childVisible);
        if (item->childCount() > 0)
            item->setExpanded(text.isEmpty() ? !collapsed.contains(pathOf(item)) : childVisible);
        return matches || childVisible;
    };
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        visit(tree_->topLevelItem(i));
    columnFit_->refit(); // hidden rows and expansion, signals blocked
}

// Selects the most deeply nested visible symbol containing the cursor.
void OutlineView::selectSymbolAtCursor()
{
    if (!editor_ || stack_->currentWidget() != tree_)
        return;
    int line = 0;
    int index = 0;
    editor_->getCursorPosition(&line, &index);

    QTreeWidgetItem *found = nullptr;
    QTreeWidgetItem *parent = tree_->invisibleRootItem();
    while (parent) {
        QTreeWidgetItem *next = nullptr;
        for (int i = 0; i < parent->childCount(); ++i) {
            QTreeWidgetItem *child = parent->child(i);
            if (!child->isHidden() && child->data(0, StartLineRole).toInt() <= line &&
                line <= child->data(0, EndLineRole).toInt()) {
                next = child;
                break;
            }
        }
        if (!next)
            break;
        found = next;
        parent = next->isExpanded() ? next : nullptr;
    }

    const QSignalBlocker blocker(tree_);
    if (found) {
        tree_->setCurrentItem(found);
        tree_->scrollToItem(found);
    } else {
        tree_->setCurrentItem(nullptr);
        tree_->clearSelection();
    }
}

void OutlineView::jumpTo(QTreeWidgetItem *item)
{
    if (!editor_ || !item)
        return;
    editor_->SendScintilla(QsciScintillaBase::SCI_GOTOPOS, item->data(0, PositionRole).toInt());
    editor_->SendScintilla(QsciScintillaBase::SCI_VERTICALCENTRECARET);
    editor_->setFocus();
}

// First symbol matching the filter (simply the first one without a filter).
QTreeWidgetItem *OutlineView::firstMatchingItem() const
{
    const QString text = filter_->text().trimmed();
    for (QTreeWidgetItemIterator it(tree_, QTreeWidgetItemIterator::NotHidden); *it; ++it) {
        if ((*it)->text(0).contains(text, Qt::CaseInsensitive))
            return *it;
    }
    return nullptr;
}

QString OutlineView::pathOf(QTreeWidgetItem *item) const
{
    QStringList names;
    for (; item; item = item->parent())
        names.prepend(item->text(0));
    return names.join(QLatin1Char('/'));
}
