#include "FindInFilesPopup.h"

#include "editor/EditorWidget.h"
#include "ui/AppFonts.h"
#include "ui/GripSplitter.h"
#include "ui/TreeMetrics.h"

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

#include <utility>

namespace {

constexpr int searchDelayMs = 300;
constexpr int matchContext = 80; // characters kept before a match on long lines

enum Role {
    MatchRole = Qt::UserRole,
    HighlightStartRole,
    HighlightLengthRole
};

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
        const int start = index.data(HighlightStartRole).toInt();
        const int length = index.data(HighlightLengthRole).toInt();
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
        painter->setPen(
            item.palette.color(item.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text));
        const qreal y = rect.top() + (rect.height() - line.height()) / 2;
        layout.draw(painter, QPointF(rect.left(), y));
        painter->restore();
    }
};

} // namespace

FindInFilesPopup::FindInFilesPopup(Theme theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("findInFiles"), QSizeF(0.72, 0.8), QSize(760, 520), parent)
    , theme_(std::move(theme))
{
    auto *title = new QLabel(tr("Find in Files"), this);
    AppFonts::setInterfaceFont(title, 1);
    summary_ = new QLabel(this);
    summary_->setEnabled(false);

    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Text to find"));
    search_->setClearButtonEnabled(true);
    search_->installEventFilter(this);
    connect(search_, &QLineEdit::textChanged, this, &FindInFilesPopup::scheduleSearch);

    matchCase_ = addOption(QStringLiteral("Cc"), tr("Match Case (Alt+C)"));
    wholeWords_ = addOption(QStringLiteral("W"), tr("Words (Alt+O)"));
    regularExpression_ = addOption(QStringLiteral(".*"), tr("Regex (Alt+X)"));

    useMask_ = new QCheckBox(tr("File mask:"), this);
    useMask_->setFocusPolicy(Qt::NoFocus);
    mask_ = new QLineEdit(this);
    mask_->setPlaceholderText(QStringLiteral("*.cpp, *.h, !*.min.js"));
    mask_->setEnabled(false);
    mask_->setMinimumWidth(mask_->fontMetrics().horizontalAdvance(mask_->placeholderText()) + 24);
    mask_->installEventFilter(this);
    connect(useMask_, &QCheckBox::toggled, this, [this](bool checked) {
        mask_->setEnabled(checked);
        if (checked)
            mask_->setFocus();
        scheduleSearch();
    });
    connect(mask_, &QLineEdit::textChanged, this, &FindInFilesPopup::scheduleSearch);

    results_ = new QTreeWidget(this);
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

    preview_ = new EditorWidget(theme_, this);
    preview_->setReadOnly(true);
    preview_->setFocusPolicy(Qt::NoFocus);

    auto *splitter = new GripSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(results_);
    splitter->addWidget(preview_);
    splitter->setSizes({300, 260});

    auto *header = new QHBoxLayout;
    header->addWidget(title);
    header->addWidget(summary_, 1);
    header->addWidget(useMask_);
    header->addWidget(mask_);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(4);
    searchRow->addWidget(search_, 1);
    searchRow->addWidget(matchCase_);
    searchRow->addWidget(wholeWords_);
    searchRow->addWidget(regularExpression_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);
    layout->addLayout(header);
    layout->addLayout(searchRow);
    layout->addWidget(splitter, 1);

    searchTimer_.setSingleShot(true);
    searchTimer_.setInterval(searchDelayMs);
    connect(&searchTimer_, &QTimer::timeout, this, &FindInFilesPopup::searchNow);
    connect(&engine_, &FindInFilesSearch::finished, this, &FindInFilesPopup::showResults);
    applyTheme(theme_);
}

QToolButton *FindInFilesPopup::addOption(const QString &text, const QString &toolTip)
{
    auto *button = new QToolButton(this);
    button->setText(text);
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
        summary_->setText(error.isEmpty() ? QString() : tr("Invalid expression: %1").arg(error));
        return;
    }
    summary_->setText(tr("Searching…"));
    engine_.start(folder_, current, buffers_ ? buffers_() : QHash<QString, QString>());
}

void FindInFilesPopup::showResults(const QList<FindMatch> &matches, bool truncated, int filesSearched)
{
    Q_UNUSED(filesSearched);
    matches_ = matches;
    QSet<QString> files;
    results_->clear();
    const QDir root(folder_);
    for (qsizetype i = 0; i < matches_.size(); ++i) {
        const FindMatch &match = matches_[i];
        files.insert(match.filePath);
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
        text = text.mid(leading);
        start -= static_cast<int>(leading);

        const QString location = QStringLiteral("%1 %2")
                                     .arg(QDir::toNativeSeparators(root.relativeFilePath(match.filePath)))
                                     .arg(match.line + 1);
        auto *item = new QTreeWidgetItem(results_, {text, location});
        item->setFont(0, theme_.font); // code, in the code font (no "->" turned into an arrow)
        item->setData(0, MatchRole, QVariant::fromValue(i));
        item->setData(0, HighlightStartRole, start);
        item->setData(0, HighlightLengthRole, match.length);
        item->setForeground(1, palette().brush(QPalette::Disabled, QPalette::Text));
        item->setToolTip(1, match.filePath);
    }
    if (search_->text().isEmpty())
        summary_->clear();
    else if (matches_.isEmpty())
        summary_->setText(tr("No matches"));
    else
        summary_->setText(tr("%1 in %2")
                              .arg(truncated              ? tr("%1+ matches").arg(matches_.size())
                                   : matches_.size() == 1 ? tr("1 match")
                                                          : tr("%1 matches").arg(matches_.size()),
                                   files.size() == 1 ? tr("1 file") : tr("%1 files").arg(files.size())));
    fitLocationColumn();
    if (results_->topLevelItemCount() > 0)
        results_->setCurrentItem(results_->topLevelItem(0));
    else
        updatePreview();
}

// The files' column as wide as its paths, at most 40 % of the list: the
// code keeps the rest.
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
    if (!item) {
        previewPath_.clear();
        preview_->showReadOnlyText(QString(), QStringLiteral("text"));
        return;
    }
    const FindMatch match = matches_.value(item->data(0, MatchRole).toLongLong());
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
    if (searchTimer_.isActive())
        return; // the results do not match the query yet
    const QTreeWidgetItem *item = results_->currentItem();
    if (!item)
        return;
    const FindMatch match = matches_.value(item->data(0, MatchRole).toLongLong());
    dismiss();
    // After the popup has closed and given the focus back.
    QTimer::singleShot(0, this,
                       [this, match] { emit matchChosen(match.filePath, match.line, match.column, match.length); });
}

void FindInFilesPopup::applyTheme(const Theme &theme)
{
    theme_ = theme;
    preview_->applyTheme(theme);
    if (auto *delegate = dynamic_cast<MatchDelegate *>(results_->itemDelegate())) {
        QColor highlight = theme.findHighlight;
        highlight.setAlpha(theme.isDark ? 70 : 60);
        delegate->highlight = highlight;
    }
    results_->viewport()->update();
}

bool FindInFilesPopup::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == search_ || watched == mask_) && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        const int row = results_->currentIndex().row();
        const int count = results_->topLevelItemCount();
        const int page = std::max(results_->viewport()->height() / std::max(results_->sizeHintForRow(0), 1) - 1, 1);
        int target = -1;
        switch (key->key()) {
        case Qt::Key_Down:
            target = std::min(row + 1, count - 1);
            break;
        case Qt::Key_Up:
            target = std::max(row - 1, 0);
            break;
        case Qt::Key_PageDown:
            target = std::min(row + page, count - 1);
            break;
        case Qt::Key_PageUp:
            target = std::max(row - page, 0);
            break;
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
            if (key->modifiers() == Qt::AltModifier) {
                QToolButton *option = key->key() == Qt::Key_C   ? matchCase_
                                      : key->key() == Qt::Key_O ? wholeWords_
                                                                : regularExpression_;
                option->toggle();
                return true;
            }
            return QFrame::eventFilter(watched, event);
        default:
            return QFrame::eventFilter(watched, event);
        }
        if (target >= 0)
            results_->setCurrentItem(results_->topLevelItem(target));
        return true;
    }
    return QFrame::eventFilter(watched, event);
}
