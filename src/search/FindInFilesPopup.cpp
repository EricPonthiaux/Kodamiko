// SPDX-License-Identifier: GPL-3.0-only

#include "FindInFilesPopup.h"

#include "editor/EditorWidget.h"
#include "ui/GripSplitter.h"
#include "ui/PopupSearchField.h"
#include "ui/SearchPopupWidgets.h"
#include "ui/TreeMetrics.h"
#include "ui/UiMetrics.h"

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTextLayout>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <optional>
#include <utility>

namespace {

constexpr int searchDelayMs = 300;
constexpr int matchContext = 80; // characters kept before a match on long lines

constexpr int matchRole = Qt::UserRole;
constexpr int highlightStartRole = Qt::UserRole + 1;
constexpr int highlightLengthRole = Qt::UserRole + 2;

// Line of a result: the occurrence stands out in bold on a colored background.
class MatchDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QColor highlight;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), treeRowHeight(option.font)));
        return size;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const int start = index.data(highlightStartRole).toInt();
        const int length = index.data(highlightLengthRole).toInt();
        if (index.column() != 0 || length <= 0) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem item = option;
        initStyleOption(&item, index);
        const QString text = item.text;
        item.text.clear();
        item.widget->style()->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);

        const QRect rect =
            item.widget->style()->subElementRect(QStyle::SE_ItemViewItemText, &item, item.widget).adjusted(3, 0, -3, 0);
        // Too long for its column: cut with "…", the occurrence kept in view
        // (lines are already shortened around it, see showResults).
        QString shown = text;
        int shownLength = length;
        QFont bold = item.font;
        bold.setBold(true);
        const QFontMetrics plain(item.font);
        const QFontMetrics strong(bold);
        const auto widthOf = [&](const QString &candidate) {
            const int end = std::min<int>(start + length, candidate.size());
            return plain.horizontalAdvance(candidate.left(start)) +
                   strong.horizontalAdvance(candidate.mid(start, end - start)) +
                   plain.horizontalAdvance(candidate.mid(end));
        };
        if (widthOf(shown) > rect.width()) {
            // The longest prefix that fits, found by bisection: painted
            // for every visible row, at every repaint.
            const QString ellipsis = QStringLiteral("…");
            int fits = start + 1; // kept even if it does not fit
            int tooLong = int(text.size());
            while (tooLong - fits > 1) {
                const int middle = fits + (tooLong - fits) / 2;
                if (widthOf(text.left(middle) + ellipsis) > rect.width())
                    tooLong = middle;
                else
                    fits = middle;
            }
            shown = text.left(fits) + ellipsis;
            shownLength = std::min(length, fits - start);
        }
        QTextLayout layout(shown, item.font);
        QTextCharFormat format;
        format.setFontWeight(QFont::Bold);
        format.setBackground(highlight);
        layout.setFormats({{start, shownLength, format}});
        layout.beginLayout();
        QTextLine line = layout.createLine();
        line.setLineWidth(1e6);
        layout.endLayout();
        painter->save();
        painter->setClipRect(rect);
        // The selection is a tint (ThemeStyle::selectionColor): the text keeps its color.
        painter->setPen(item.palette.color(QPalette::Text));
        const qreal y = rect.top() + (rect.height() - line.height()) / 2;
        layout.draw(painter, QPointF(rect.left(), y));
        painter->restore();
    }
};

// What a result row shows of its line, and where the occurrence starts in it.
struct Excerpt {
    QString text;
    int highlightStart = 0;
};

Excerpt excerptOf(const FindMatch &match)
{
    // Long lines: the text around the occurrence.
    QString text = match.lineText;
    int start = match.column;
    if (start > matchContext) {
        const int cut = start - matchContext / 2;
        text = QStringLiteral("…") + text.mid(cut);
        start = start - cut + 1;
    }
    // And no more than a column can show after it (minified files).
    if (text.size() > start + match.length + matchContext * 4)
        text = text.left(start + match.length + matchContext * 4) + QStringLiteral("…");
    // Leading whitespace is dropped, as in IntelliJ.
    qsizetype leading = 0;
    while (leading < text.size() && leading < start && text.at(leading).isSpace())
        ++leading;
    return {text.mid(leading), start - static_cast<int>(leading)};
}

// Row a navigation key moves the selection to (-1 when the list is empty),
// nullopt for any other key.
std::optional<int> navigationTarget(int key, int row, int count, int page)
{
    switch (key) {
    case Qt::Key_Down:
        return std::min(row + 1, count - 1);
    case Qt::Key_Up:
        return std::max(row - 1, 0);
    case Qt::Key_PageDown:
        return std::min(row + page, count - 1);
    case Qt::Key_PageUp:
        return std::max(row - page, 0);
    default:
        return std::nullopt;
    }
}

} // namespace

FindInFilesPopup::FindInFilesPopup(Theme theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("findInFiles"), QSizeF(0.68, 0.72), QSize(720, 480), parent)
    , theme_(std::move(theme))
{
    setBehavior(Behavior::Transient);
    setAccessibleName(tr("Find in Files"));

    search_ = new PopupSearchField(this);
    search_->setPlaceholderText(tr("Find in files"));
    search_->setAccessibleName(tr("Text to find"));
    search_->installEventFilter(this);
    connect(search_, &QLineEdit::textChanged, this, &FindInFilesPopup::scheduleSearch);

    matchCase_ = addOption(QStringLiteral("Aa"), tr("Match Case (Alt+C)"));
    wholeWords_ = addOption(QStringLiteral("W"), tr("Whole Words (Alt+O)"));
    regularExpression_ = addOption(QStringLiteral(".*"), tr("Regular Expression (Alt+X)"));
    QWidget *searchBar = SearchPopup::searchArea(search_, this);
    for (QToolButton *option : {matchCase_, wholeWords_, regularExpression_})
        searchBar->layout()->addWidget(option);

    auto *splitter = new GripSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(createResultsPane());
    previewPane_ = createPreviewPane();
    splitter->addWidget(previewPane_);
    splitter->setSizes({300, 240});

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(searchBar);
    layout->addWidget(createFilters());
    layout->addWidget(splitter, 1);
    layout->addWidget(SearchPopup::footer(
        {{tr("↑ ↓"), tr("Preview")}, {tr("Enter"), tr("Open")}, {tr("Esc"), tr("Close")}}, summary_, this));

    searchTimer_.setSingleShot(true);
    searchTimer_.setInterval(searchDelayMs);
    connect(&searchTimer_, &QTimer::timeout, this, &FindInFilesPopup::searchNow);
    connect(&engine_, &FindInFilesSearch::finished, this, &FindInFilesPopup::showResults);
    applyTheme(theme_);
}

QWidget *FindInFilesPopup::createFilters()
{
    useMask_ = new QCheckBox(tr("File mask:"), this);
    useMask_->setFocusPolicy(Qt::NoFocus);
    mask_ = new QLineEdit(this);
    mask_->setPlaceholderText(QStringLiteral("*.cpp, *.h, !*.min.js"));
    mask_->setEnabled(false);
    mask_->setObjectName(QStringLiteral("fileMask"));
    mask_->setAccessibleName(tr("File mask"));
    mask_->installEventFilter(this);
    connect(useMask_, &QCheckBox::toggled, this, [this](bool checked) {
        mask_->setEnabled(checked);
        if (checked)
            mask_->setFocus();
        scheduleSearch();
    });
    connect(mask_, &QLineEdit::textChanged, this, &FindInFilesPopup::scheduleSearch);

    auto *filters = new QFrame(this);
    filters->setObjectName(QStringLiteral("searchFilters"));
    auto *layout = new QHBoxLayout(filters);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(8);
    layout->addWidget(useMask_);
    layout->addWidget(mask_, 1);
    return filters;
}

QWidget *FindInFilesPopup::createResultsPane()
{
    results_ = new QTreeWidget(this);
    results_->setAccessibleName(tr("Search results"));
    results_->setObjectName(QStringLiteral("popupList"));
    results_->setColumnCount(2);
    results_->setHeaderHidden(true);
    results_->setRootIsDecorated(false);
    results_->setUniformRowHeights(true);
    results_->setFocusPolicy(Qt::NoFocus);
    // Long paths lose their start, not the file name.
    results_->setTextElideMode(Qt::ElideLeft);
    results_->header()->setStretchLastSection(false);
    results_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    results_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    results_->setItemDelegate(new MatchDelegate(results_));
    connect(results_, &QTreeWidget::currentItemChanged, this, &FindInFilesPopup::updatePreview);
    connect(results_, &QTreeWidget::itemDoubleClicked, this, &FindInFilesPopup::openCurrent);

    auto *pane = new QWidget(this);
    summary_ = SearchPopup::resultSummary(this);
    empty_ = SearchPopup::emptyState(pane);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(results_, 1);
    layout->addWidget(empty_, 1);
    return pane;
}

QWidget *FindInFilesPopup::createPreviewPane()
{
    auto *pane = new QWidget(this);
    auto *header = new QFrame(pane);
    header->setObjectName(QStringLiteral("searchPreviewHeader"));
    previewLocation_ = new QLabel(header);
    previewLocation_->setObjectName(QStringLiteral("searchPreviewPath"));
    previewLocation_->setTextFormat(Qt::PlainText);
    previewLocation_->installEventFilter(this);
    previewLocation_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *heading = new QHBoxLayout(header);
    heading->setContentsMargins(10, 4, 10, 4);
    heading->addWidget(previewLocation_, 1);
    preview_ = new EditorWidget(theme_, pane);
    preview_->setReadOnly(true);
    preview_->setFocusPolicy(Qt::NoFocus);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(preview_, 1);
    return pane;
}

void FindInFilesPopup::setEmptyMessage(const QString &text)
{
    empty_->setText(text);
    empty_->show();
    results_->hide();
    previewPane_->hide();
}

QToolButton *FindInFilesPopup::addOption(const QString &text, const QString &toolTip)
{
    auto *button = new QToolButton(this);
    button->setObjectName(QStringLiteral("searchOption"));
    button->setText(text);
    button->setAccessibleName(toolTip);
    UiMetrics::setScaledSize(button, QSize(28, 28));
    button->setToolTip(toolTip);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    connect(button, &QToolButton::toggled, this, &FindInFilesPopup::scheduleSearch);
    return button;
}

void FindInFilesPopup::setFolder(const QString &folder)
{
    folder_ = folder;
    engine_.cancel();
    matches_.clear();
    results_->clear();
}

void FindInFilesPopup::setBufferProvider(std::function<QHash<QString, QString>()> provider)
{
    buffers_ = std::move(provider);
}

void FindInFilesPopup::setOptions(bool matchCase, bool wholeWords, bool regularExpression)
{
    matchCase_->setChecked(matchCase);
    wholeWords_->setChecked(wholeWords);
    regularExpression_->setChecked(regularExpression);
}

void FindInFilesPopup::popup(const QString &text)
{
    if (!text.isEmpty() && !text.contains(QLatin1Char('\n')))
        search_->setText(regularExpression_->isChecked() ? QRegularExpression::escape(text) : text);
    showCentered();
    search_->setFocus();
    search_->selectAll();
    searchNow(); // the files may have changed since the last time
}

FindQuery FindInFilesPopup::query() const
{
    FindQuery query;
    query.text = search_->text();
    query.matchCase = matchCase_->isChecked();
    query.wholeWords = wholeWords_->isChecked();
    query.regularExpression = regularExpression_->isChecked();
    if (useMask_->isChecked())
        query.fileMask = mask_->text();
    return query;
}

void FindInFilesPopup::scheduleSearch()
{
    searchTimer_.start();
}

void FindInFilesPopup::searchNow()
{
    searchTimer_.stop();
    const FindQuery current = query();
    QString error;
    if (!current.compile(&error)) {
        engine_.cancel();
        matches_.clear();
        results_->clear();
        updatePreview();
        summary_->setText(error.isEmpty() ? tr("Project search") : tr("Invalid regular expression"));
        setEmptyMessage(error.isEmpty() ? tr("Type to search in files") : error);
        return;
    }
    matches_.clear();
    results_->clear();
    summary_->setText(tr("Searching…"));
    setEmptyMessage(tr("Searching your project…"));
    engine_.start(folder_, current, buffers_ ? buffers_() : QHash<QString, QString>());
}

void FindInFilesPopup::showResults(const QList<FindMatch> &matches, bool truncated, int filesSearched)
{
    matches_ = matches;
    QSet<QString> files;
    results_->clear();
    const QDir root(folder_);
    for (qsizetype i = 0; i < matches_.size(); ++i) {
        const FindMatch &match = matches_[i];
        files.insert(match.filePath);
        const Excerpt excerpt = excerptOf(match);
        const QString location = QStringLiteral("%1:%2")
                                     .arg(QDir::toNativeSeparators(root.relativeFilePath(match.filePath)))
                                     .arg(match.line + 1);
        auto *item = new QTreeWidgetItem(results_, {excerpt.text, location});
        item->setFont(0, theme_.font); // code, in the code font (no "->" turned into an arrow)
        item->setData(0, matchRole, QVariant::fromValue(i));
        item->setData(0, highlightStartRole, excerpt.highlightStart);
        item->setData(0, highlightLengthRole, match.length);
        item->setForeground(1, theme_.mutedText);
        item->setToolTip(1, match.filePath);
    }
    if (search_->text().isEmpty())
        summary_->clear();
    else
        summary_->setText(summaryText(truncated, files.size()));
    summary_->setToolTip(filesSearched == 1 ? tr("1 file searched") : tr("%1 files searched").arg(filesSearched));
    results_->setVisible(!matches_.isEmpty());
    empty_->setVisible(matches_.isEmpty());
    if (matches_.isEmpty())
        setEmptyMessage(tr("No matches found"));
    fitLocationColumn();
    if (results_->topLevelItemCount() > 0)
        results_->setCurrentItem(results_->topLevelItem(0));
    else
        updatePreview();
}

// The files' column as wide as its paths, at most 40 % of the list: the
// code keeps the rest.
QString FindInFilesPopup::summaryText(bool truncated, qsizetype fileCount) const
{
    if (matches_.isEmpty())
        return tr("No matches");
    QString matchCount;
    if (truncated)
        matchCount = tr("%1+ matches").arg(matches_.size());
    else if (matches_.size() == 1)
        matchCount = tr("1 match");
    else
        matchCount = tr("%1 matches").arg(matches_.size());
    const QString files = fileCount == 1 ? tr("1 file") : tr("%1 files").arg(fileCount);
    return tr("%1 in %2").arg(matchCount, files);
}

void FindInFilesPopup::fitLocationColumn()
{
    const QFontMetrics metrics(results_->font());
    int wanted = 0;
    for (int i = 0; i < results_->topLevelItemCount(); ++i)
        wanted = std::max(wanted, metrics.horizontalAdvance(results_->topLevelItem(i)->text(1)));
    wanted += 24;
    results_->header()->resizeSection(1, std::min(wanted, results_->viewport()->width() * 2 / 5));
}

void FindInFilesPopup::resizeEvent(QResizeEvent *event)
{
    OverlayPopup::resizeEvent(event);
    fitLocationColumn();
}

void FindInFilesPopup::updatePreview()
{
    const QTreeWidgetItem *item = results_->currentItem();
    previewPane_->setVisible(item != nullptr);
    if (!item) {
        previewLocation_->clear();
        previewPath_.clear();
        preview_->showReadOnlyText(QString(), QStringLiteral("text"));
        return;
    }
    const FindMatch match = matches_.value(item->data(0, matchRole).toLongLong());
    const QString location =
        QStringLiteral("%1:%2").arg(QDir(folder_).relativeFilePath(match.filePath)).arg(match.line + 1);
    previewLocation_->setToolTip(location);
    previewLocation_->setText(
        previewLocation_->fontMetrics().elidedText(location, Qt::ElideMiddle, previewLocation_->width()));
    if (match.filePath != previewPath_) {
        previewPath_ = match.filePath;
        QString error;
        preview_->setReadOnly(false);
        if (!preview_->load(match.filePath, &error))
            preview_->showReadOnlyText(error, QStringLiteral("text"));
        preview_->setReadOnly(true);
        // The editor's unsaved text, which is what was searched.
        if (buffers_) {
            const QHash<QString, QString> buffers = buffers_();
            if (const auto buffer = buffers.constFind(match.filePath); buffer != buffers.cend()) {
                preview_->setReadOnly(false);
                preview_->setText(*buffer);
                preview_->setReadOnly(true);
            }
        }
    }
    const int line = std::min(match.line, std::max(preview_->lines() - 1, 0));
    const QString text = preview_->text(line);
    const long lineStart = preview_->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, line);
    const long start = lineStart + text.left(match.column).toUtf8().size();
    const long end = lineStart + text.left(match.column + match.length).toUtf8().size();
    preview_->SendScintilla(QsciScintillaBase::SCI_SETSEL, start, end);
    preview_->SendScintilla(QsciScintillaBase::SCI_VERTICALCENTRECARET);
}

void FindInFilesPopup::openCurrent()
{
    if (searchTimer_.isActive() || engine_.isRunning())
        return; // the results do not match the query yet
    const QTreeWidgetItem *item = results_->currentItem();
    if (!item)
        return;
    const FindMatch match = matches_.value(item->data(0, matchRole).toLongLong());
    dismiss();
    // After the popup has closed and given the focus back.
    QTimer::singleShot(0, this,
                       [this, match] { emit matchChosen(match.filePath, match.line, match.column, match.length); });
}

void FindInFilesPopup::applyTheme(const Theme &theme)
{
    theme_ = theme;
    setStyleSheet(SearchPopup::styleSheet(theme));
    preview_->applyTheme(theme);
    if (auto *delegate = dynamic_cast<MatchDelegate *>(results_->itemDelegate())) {
        QColor highlight = theme.findHighlight;
        highlight.setAlpha(theme.isDark ? 70 : 60);
        delegate->highlight = highlight;
    }
    for (int row = 0; row < results_->topLevelItemCount(); ++row) {
        results_->topLevelItem(row)->setFont(0, theme.font);
        results_->topLevelItem(row)->setForeground(1, theme.mutedText);
    }
    results_->viewport()->update();
}

bool FindInFilesPopup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == previewLocation_ && event->type() == QEvent::Resize)
        previewLocation_->setText(previewLocation_->fontMetrics().elidedText(
            previewLocation_->toolTip(), Qt::ElideMiddle, previewLocation_->width()));
    const bool isSearchField = watched == search_ || watched == mask_;
    if (!isSearchField || event->type() != QEvent::KeyPress)
        return QFrame::eventFilter(watched, event);
    if (handleSearchFieldKey(*static_cast<QKeyEvent *>(event)))
        return true;
    return QFrame::eventFilter(watched, event);
}

// Keys typed in the search fields that act on the results or the options.
bool FindInFilesPopup::handleSearchFieldKey(const QKeyEvent &key)
{
    const int row = results_->currentIndex().row();
    const int count = results_->topLevelItemCount();
    const int page = std::max(results_->viewport()->height() / std::max(results_->sizeHintForRow(0), 1) - 1, 1);
    if (const std::optional<int> target = navigationTarget(key.key(), row, count, page)) {
        if (*target >= 0)
            results_->setCurrentItem(results_->topLevelItem(*target));
        return true;
    }
    switch (key.key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (searchTimer_.isActive())
            searchNow();
        else
            openCurrent();
        return true;
    case Qt::Key_Escape:
        dismiss();
        return true;
    case Qt::Key_C:
    case Qt::Key_O:
    case Qt::Key_X:
        if (key.modifiers() != Qt::AltModifier)
            return false;
        optionForShortcut(key.key())->toggle();
        return true;
    default:
        return false;
    }
}

// Alt+C, Alt+O, Alt+X: match case, whole words, regular expression.
QToolButton *FindInFilesPopup::optionForShortcut(int key) const
{
    switch (key) {
    case Qt::Key_C:
        return matchCase_;
    case Qt::Key_O:
        return wholeWords_;
    default:
        return regularExpression_;
    }
}
