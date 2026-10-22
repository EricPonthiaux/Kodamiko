// SPDX-License-Identifier: GPL-3.0-only

#include "FindBar.h"

#include "EditorWidget.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <ranges>

namespace {

// Indicator of the highlighted matches (the first ones belong to lexers).
constexpr int matchIndicator = 9;
constexpr int matchLimit = 10000;
constexpr int refreshDelayMs = 150;

QString errorColor(const Theme &theme)
{
    return theme.isDark ? QStringLiteral("#e06c75") : QStringLiteral("#e45649");
}

} // namespace

FindBar::FindBar(QWidget *parent)
    : QWidget(parent)
    , theme_(Theme::dark())
{
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Find"));
    search_->setClearButtonEnabled(true);
    search_->installEventFilter(this);
    connect(search_, &QLineEdit::textChanged, this, [this] {
        refresh();
        if (editor_)
            selectFrom(std::min(editor_->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONSTART),
                                editor_->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONEND)),
                       true);
    });

    matchCase_ = addOption(QStringLiteral("Cc"), tr("Match Case (Alt+C)"));
    wholeWords_ = addOption(QStringLiteral("W"), tr("Words (Alt+O)"));
    regularExpression_ = addOption(QStringLiteral(".*"), tr("Regex (Alt+X)"));

    count_ = new QLabel(this);
    count_->setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("0000/0000")));

    const auto addButton = [this](const QString &text, const QString &toolTip) {
        auto *button = new QToolButton(this);
        button->setText(text);
        button->setToolTip(toolTip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };
    QToolButton *previous = addButton(QStringLiteral("↑"), tr("Previous Occurrence (Shift+F3)"));
    connect(previous, &QToolButton::clicked, this, &FindBar::findPrevious);
    QToolButton *next = addButton(QStringLiteral("↓"), tr("Next Occurrence (F3)"));
    connect(next, &QToolButton::clicked, this, &FindBar::findNext);
    QToolButton *closeButton = addButton(QStringLiteral("✕"), tr("Close (Escape)"));
    connect(closeButton, &QToolButton::clicked, this, &FindBar::dismiss);

    auto *findRow = new QHBoxLayout;
    findRow->setSpacing(4);
    findRow->addWidget(search_, 1);
    findRow->addWidget(matchCase_);
    findRow->addWidget(wholeWords_);
    findRow->addWidget(regularExpression_);
    findRow->addWidget(count_);
    findRow->addWidget(previous);
    findRow->addWidget(next);
    findRow->addStretch();
    findRow->addWidget(closeButton);

    replace_ = new QLineEdit(this);
    replace_->setPlaceholderText(tr("Replace"));
    replace_->installEventFilter(this);
    auto *replaceOne = new QPushButton(tr("&Replace"), this);
    replaceOne->setFocusPolicy(Qt::NoFocus);
    connect(replaceOne, &QPushButton::clicked, this, &FindBar::replaceCurrent);
    auto *replaceEvery = new QPushButton(tr("Replace &All"), this);
    replaceEvery->setFocusPolicy(Qt::NoFocus);
    connect(replaceEvery, &QPushButton::clicked, this, &FindBar::replaceAll);
    replaceRow_ = new QWidget(this);
    auto *replaceRow = new QHBoxLayout(replaceRow_);
    replaceRow->setContentsMargins(0, 0, 0, 0);
    replaceRow->setSpacing(4);
    replaceRow->addWidget(replace_, 1);
    replaceRow->addWidget(replaceOne);
    replaceRow->addWidget(replaceEvery);
    replaceRow->addStretch();

    // The fields share the width of the search field's row, IntelliJ-style.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);
    layout->addLayout(findRow);
    layout->addWidget(replaceRow_);

    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(refreshDelayMs);
    connect(&refreshTimer_, &QTimer::timeout, this, &FindBar::refresh);
    hide();
}

QToolButton *FindBar::addOption(const QString &text, const QString &toolTip)
{
    auto *button = new QToolButton(this);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    connect(button, &QToolButton::toggled, this, [this] {
        refresh();
        if (editor_)
            selectFrom(std::min(editor_->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONSTART),
                                editor_->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONEND)),
                       true);
    });
    return button;
}

void FindBar::setEditor(EditorWidget *editor)
{
    if (editor == editor_)
        return;
    if (editor_) {
        clearHighlights(editor_);
        disconnect(editor_, nullptr, this, nullptr);
    }
    editor_ = editor;
    if (editor_)
        connect(editor_, &QsciScintilla::textChanged, this, &FindBar::scheduleRefresh);
    matches_.clear();
    if (isVisible())
        refresh();
}

void FindBar::showFind()
{
    open(false);
}

void FindBar::showReplace()
{
    open(true);
}

void FindBar::open(bool withReplace)
{
    if (editor_ && editor_->hasSelectedText()) {
        const QString selected = editor_->selectedText();
        if (!selected.contains(QLatin1Char('\n')) && !selected.contains(QLatin1Char('\r'))) {
            const QSignalBlocker blocker(search_);
            search_->setText(regularExpression_->isChecked() ? QRegularExpression::escape(selected) : selected);
        }
    }
    replaceRow_->setVisible(withReplace);
    show();
    search_->setFocus();
    search_->selectAll();
    refresh();
}

void FindBar::dismiss()
{
    refreshTimer_.stop();
    hide();
    if (editor_) {
        clearHighlights(editor_);
        editor_->setFocus();
    }
    matches_.clear();
}

void FindBar::setOptions(bool matchCase, bool wholeWords, bool regularExpression)
{
    const QSignalBlocker caseBlocker(matchCase_);
    const QSignalBlocker wordsBlocker(wholeWords_);
    const QSignalBlocker regexBlocker(regularExpression_);
    matchCase_->setChecked(matchCase);
    wholeWords_->setChecked(wholeWords);
    regularExpression_->setChecked(regularExpression);
    refresh();
}

int FindBar::searchFlags() const
{
    int flags = 0;
    if (matchCase_->isChecked())
        flags |= QsciScintillaBase::SCFIND_MATCHCASE;
    if (wholeWords_->isChecked())
        flags |= QsciScintillaBase::SCFIND_WHOLEWORD;
    if (regularExpression_->isChecked())
        flags |= QsciScintillaBase::SCFIND_REGEXP | QsciScintillaBase::SCFIND_CXX11REGEX;
    return flags;
}

std::optional<QList<FindBar::Match>> FindBar::findAll(EditorWidget *editor, const Query &query)
{
    QList<Match> matches;
    if (!editor || query.text.isEmpty())
        return matches;
    const QByteArray pattern = query.text.toUtf8();
    const long length = editor->SendScintilla(QsciScintillaBase::SCI_GETLENGTH);
    editor->SendScintilla(QsciScintillaBase::SCI_SETSEARCHFLAGS, query.flags);
    editor->SendScintilla(QsciScintillaBase::SCI_SETSTATUS, 0);
    long position = 0;
    while (position <= length && matches.size() < query.limit) {
        editor->SendScintilla(QsciScintillaBase::SCI_SETTARGETRANGE, static_cast<unsigned long>(position), length);
        const long found = editor->SendScintilla(QsciScintillaBase::SCI_SEARCHINTARGET,
                                                 uintptr_t(pattern.size()), pattern.constData());
        if (found < 0)
            break;
        const long end = editor->SendScintilla(QsciScintillaBase::SCI_GETTARGETEND);
        if (end > found)
            matches.append({found, end});
        // An empty match (regular expression "^"…): move on by one character.
        position = end > found ? end : editor->SendScintilla(QsciScintillaBase::SCI_POSITIONAFTER, end);
        if (end == found && position == end)
            break;
    }
    if (editor->SendScintilla(QsciScintillaBase::SCI_GETSTATUS) == QsciScintillaBase::SC_STATUS_WARNREGEX) {
        editor->SendScintilla(QsciScintillaBase::SCI_SETSTATUS, 0);
        return std::nullopt;
    }
    return matches;
}

void FindBar::scheduleRefresh()
{
    if (isVisible())
        refreshTimer_.start();
}

void FindBar::refresh()
{
    refreshTimer_.stop();
    if (!editor_) {
        matches_.clear();
        valid_ = true;
        updateCount();
        return;
    }
    const std::optional<QList<Match>> found =
        findAll(editor_, {.text = search_->text(), .flags = searchFlags(), .limit = matchLimit});
    valid_ = found.has_value();
    matches_ = found.value_or(QList<Match>());

    clearHighlights(editor_);
    const bool dark = theme_.isDark;
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICSETSTYLE, matchIndicator, QsciScintillaBase::INDIC_ROUNDBOX);
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICSETFORE, matchIndicator, theme_.findHighlight);
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICSETALPHA, matchIndicator, dark ? 70 : 90);
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICSETOUTLINEALPHA, matchIndicator, 160);
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICSETUNDER, matchIndicator, true);
    editor_->SendScintilla(QsciScintillaBase::SCI_SETINDICATORCURRENT, matchIndicator);
    for (const Match &match : std::as_const(matches_))
        editor_->SendScintilla(QsciScintillaBase::SCI_INDICATORFILLRANGE, match.start, match.end - match.start);
    updateCount();
}

void FindBar::clearHighlights(EditorWidget *editor)
{
    editor->SendScintilla(QsciScintillaBase::SCI_SETINDICATORCURRENT, matchIndicator);
    editor->SendScintilla(QsciScintillaBase::SCI_INDICATORCLEARRANGE, 0,
                          editor->SendScintilla(QsciScintillaBase::SCI_GETLENGTH));
}

int FindBar::currentMatch() const
{
    if (!editor_)
        return 0;
    const long start = editor_->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONSTART);
    const long end = editor_->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONEND);
    // Matches are in text order and do not overlap.
    const auto it = std::lower_bound(matches_.cbegin(), matches_.cend(), start,
                                     [](const Match &match, long position) { return match.start < position; });
    if (it != matches_.cend() && it->start == start && it->end == end)
        return static_cast<int>(it - matches_.cbegin() + 1);
    return 0;
}

void FindBar::updateCount()
{
    const bool error = !valid_ || (!search_->text().isEmpty() && matches_.isEmpty());
    // Updated at each keystroke: a style sheet is only set when it changes.
    const QString sheet =
        error ? QStringLiteral("QLineEdit { border: 1px solid %1; }").arg(errorColor(theme_)) : QString();
    if (sheet != search_->styleSheet())
        search_->setStyleSheet(sheet);
    if (!valid_)
        count_->setText(tr("Bad pattern"));
    else if (search_->text().isEmpty())
        count_->clear();
    else if (matches_.isEmpty())
        count_->setText(tr("0 results"));
    else if (const int current = currentMatch(); current > 0)
        count_->setText(QStringLiteral("%1/%2").arg(current).arg(matches_.size()));
    else
        count_->setText(matches_.size() == 1 ? tr("1 result") : tr("%1 results").arg(matches_.size()));
    count_->setStyleSheet(error ? QStringLiteral("color: %1").arg(errorColor(theme_)) : QString());
}

void FindBar::select(const Match &match)
{
    editor_->SendScintilla(QsciScintillaBase::SCI_SETSEL, match.start, match.end);
    const long line = editor_->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, match.start);
    editor_->ensureLineVisible(static_cast<int>(line));
    updateCount();
}

void FindBar::selectFrom(long position, bool forward)
{
    if (!editor_ || matches_.isEmpty()) {
        updateCount();
        return;
    }
    if (forward) {
        for (const Match &match : std::as_const(matches_)) {
            if (match.start >= position) {
                select(match);
                return;
            }
        }
        select(matches_.first()); // wraps around
    } else {
        for (const Match &match : std::views::reverse(matches_)) {
            if (match.end <= position) {
                select(match);
                return;
            }
        }
        select(matches_.last());
    }
}

void FindBar::findNext()
{
    if (!editor_)
        return;
    if (!isVisible()) {
        showFind();
        return;
    }
    if (refreshTimer_.isActive())
        refresh();
    const long start = editor_->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONSTART);
    const long end = editor_->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONEND);
    // Past the selected match; from the cursor otherwise.
    selectFrom(currentMatch() > 0 ? std::max(end, start + 1) : start, true);
}

void FindBar::findPrevious()
{
    if (!editor_)
        return;
    if (!isVisible()) {
        showFind();
        return;
    }
    if (refreshTimer_.isActive())
        refresh();
    selectFrom(editor_->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONSTART), false);
}

void FindBar::replaceCurrent()
{
    if (!editor_ || editor_->isReadOnly())
        return;
    if (refreshTimer_.isActive())
        refresh();
    const int current = currentMatch();
    if (current == 0) {
        findNext();
        return;
    }
    const Match match = matches_.at(current - 1);
    const QByteArray pattern = search_->text().toUtf8();
    const QByteArray replacement = replace_->text().toUtf8();
    // The target must come from a search for "\1"-style references to work.
    editor_->SendScintilla(QsciScintillaBase::SCI_SETSEARCHFLAGS, searchFlags());
    editor_->SendScintilla(QsciScintillaBase::SCI_SETTARGETRANGE, static_cast<unsigned long>(match.start), match.end);
    if (editor_->SendScintilla(QsciScintillaBase::SCI_SEARCHINTARGET, uintptr_t(pattern.size()),
                               pattern.constData()) < 0)
        return;
    const int message =
        regularExpression_->isChecked() ? QsciScintillaBase::SCI_REPLACETARGETRE : QsciScintillaBase::SCI_REPLACETARGET;
    const long replacedLength =
        editor_->SendScintilla(message, uintptr_t(replacement.size()), replacement.constData());
    refresh();
    selectFrom(match.start + replacedLength, true);
}

void FindBar::replaceAll()
{
    if (!editor_ || editor_->isReadOnly())
        return;
    refresh();
    if (matches_.isEmpty())
        return;
    const QByteArray pattern = search_->text().toUtf8();
    const QByteArray replacement = replace_->text().toUtf8();
    const int message =
        regularExpression_->isChecked() ? QsciScintillaBase::SCI_REPLACETARGETRE : QsciScintillaBase::SCI_REPLACETARGET;
    const int replaced = static_cast<int>(matches_.size());
    editor_->SendScintilla(QsciScintillaBase::SCI_SETSEARCHFLAGS, searchFlags());
    editor_->beginUndoAction();
    // From the end: the positions of the remaining matches do not move.
    for (const Match &match : std::views::reverse(matches_)) {
        editor_->SendScintilla(QsciScintillaBase::SCI_SETTARGETRANGE, static_cast<unsigned long>(match.start),
                               match.end);
        if (editor_->SendScintilla(QsciScintillaBase::SCI_SEARCHINTARGET, uintptr_t(pattern.size()),
                                   pattern.constData()) >= 0)
            editor_->SendScintilla(message, uintptr_t(replacement.size()), replacement.constData());
    }
    editor_->endUndoAction();
    refresh();
    count_->setText(tr("%1 replaced").arg(replaced));
}

void FindBar::applyTheme(const Theme &theme)
{
    theme_ = theme;
    if (isVisible())
        refresh();
}

bool FindBar::eventFilter(QObject *watched, QEvent *event)
{
    const bool isField = watched == search_ || watched == replace_;
    if (isField && event->type() == QEvent::KeyPress && handleFieldKey(watched, *static_cast<QKeyEvent *>(event)))
        return true;
    return QWidget::eventFilter(watched, event);
}

bool FindBar::handleFieldKey(const QObject *field, const QKeyEvent &key)
{
    switch (key.key()) {
    case Qt::Key_Escape:
        dismiss();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (field == replace_)
            replaceCurrent();
        else if (key.modifiers() & Qt::ShiftModifier)
            findPrevious();
        else
            findNext();
        return true;
    case Qt::Key_C:
    case Qt::Key_O:
    case Qt::Key_X:
        if (key.modifiers() != Qt::AltModifier)
            return false;
        optionForKey(key.key())->toggle();
        return true;
    default:
        return false;
    }
}

// The option that Alt+`key` toggles, as its tooltip says.
QToolButton *FindBar::optionForKey(int key) const
{
    if (key == Qt::Key_C)
        return matchCase_;
    if (key == Qt::Key_O)
        return wholeWords_;
    return regularExpression_;
}
