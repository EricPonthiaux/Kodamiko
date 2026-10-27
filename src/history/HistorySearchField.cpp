// SPDX-License-Identifier: GPL-3.0-only

#include "HistorySearchField.h"

#include "ui/UiIcons.h"

#include <QCoreApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QTextCharFormat>

#include <algorithm>
#include <array>

namespace {

constexpr int maximumWidthPx = 520;

// A term the menu inserts, and how the menu names it.
struct TermKind {
    const char *key;
    const char *title;
};

constexpr std::array termKinds = {
    TermKind{"author:", QT_TRANSLATE_NOOP("HistorySearchField", "Author")},
    TermKind{"message:", QT_TRANSLATE_NOOP("HistorySearchField", "Message")},
    TermKind{"after:", QT_TRANSLATE_NOOP("HistorySearchField", "After Date")},
    TermKind{"before:", QT_TRANSLATE_NOOP("HistorySearchField", "Before Date")},
    TermKind{"contents:", QT_TRANSLATE_NOOP("HistorySearchField", "Changes Adding or Removing Text (git log -S)")},
    TermKind{"diff:", QT_TRANSLATE_NOOP("HistorySearchField", "Changed Lines Matching a Regex (git log -G)")},
};

QString syntaxHelp()
{
    return QObject::tr("Words: in the message, the author, the ID or a branch name.\n"
                       "author:ada — the author's name or email\n"
                       "message:\"null check\" — the whole message\n"
                       "after:2024-03-01, before:2024-06-30 — days included; also 7d, 2w, 3m, 1y\n"
                       "contents:parseUrl — changes adding or removing the text (git log -S)\n"
                       "diff:\"TODO\\(\\w+\\)\" — changed lines matching the regex (git log -G)\n"
                       "Everything typed is required; contents: and diff: are case sensitive.\n"
                       "Backspace right after a term removes it whole.");
}

} // namespace

HistorySearchField::HistorySearchField(QWidget *parent)
    : QLineEdit(parent)
{
    setPlaceholderText(tr("Search: text, author:, message:, after:, contents:…"));
    setClearButtonEnabled(true);
    setMaximumWidth(maximumWidthPx);
    setToolTip(syntaxHelp());
    searchIcon_ = addAction(QIcon(), QLineEdit::LeadingPosition);
    termsButton_ = addAction(QIcon(), QLineEdit::TrailingPosition);
    termsButton_->setToolTip(tr("Add a Filter"));
    connect(termsButton_, &QAction::triggered, this, &HistorySearchField::showTermsMenu);
    connect(this, &QLineEdit::textChanged, this, &HistorySearchField::onTextChanged);
}

void HistorySearchField::applyTheme(const Theme &theme)
{
    searchIcon_->setIcon(UiIcons::icon(QStringLiteral("search"), theme.mutedText));
    termsButton_->setIcon(UiIcons::icon(QStringLiteral("filter"), theme.mutedText));
    errorColor_ = theme.error;
    chipBackground_ = theme.accent;
    chipBackground_.setAlphaF(0.22F);
    chipText_ = theme.isDark ? theme.accent.lighter(140) : theme.accent.darker(115);
    showError();
    showTerms();
}

void HistorySearchField::onTextChanged()
{
    query_ = CommitQuery::parse(text());
    showError();
    showTerms();
    emit queryChanged();
}

// The kinds of terms to add, then those typed, to remove.
void HistorySearchField::showTermsMenu()
{
    QMenu menu;
    for (const TermKind &kind : termKinds) {
        const QString key = QString::fromLatin1(kind.key);
        menu.addAction(tr(kind.title), this, [this, key] { insertTerm(key); });
    }
    if (!query_.termSpans.isEmpty())
        menu.addSeparator();
    for (qsizetype i = 0; i < query_.termSpans.size(); ++i) {
        const CommitQuery::Span &span = query_.termSpans.at(i);
        menu.addAction(tr("Remove %1").arg(text().mid(span.start, span.length)), this, [this, i] { removeTerm(i); });
    }
    menu.exec(mapToGlobal(QPoint(width() - menu.sizeHint().width(), height())));
}

void HistorySearchField::removeTerm(qsizetype index)
{
    if (index < 0 || index >= query_.termSpans.size())
        return;
    const CommitQuery::Span span = query_.termSpans.at(index);
    QString typed = text();
    qsizetype start = span.start;
    qsizetype end = span.start + span.length;
    // One space goes with it: the words around it stay apart.
    if (end < typed.size() && typed.at(end) == u' ')
        ++end;
    else if (start > 0 && typed.at(start - 1) == u' ')
        --start;
    setText(typed.remove(start, end - start));
    setCursorPosition(int(start));
}

void HistorySearchField::keyPressEvent(QKeyEvent *event)
{
    const bool isPlainBackspace = event->key() == Qt::Key_Backspace && event->modifiers() == Qt::NoModifier;
    if (!isPlainBackspace || hasSelectedText()) {
        QLineEdit::keyPressEvent(event);
        return;
    }
    // Right after a chip: the chip goes whole.
    const auto term = std::ranges::find_if(query_.termSpans, [this](const CommitQuery::Span &span) {
        return span.start + span.length == cursorPosition();
    });
    if (term == query_.termSpans.end()) {
        QLineEdit::keyPressEvent(event);
        return;
    }
    removeTerm(term - query_.termSpans.begin());
}

void HistorySearchField::insertTerm(const QString &key)
{
    const bool needsSpace = !text().isEmpty() && !text().endsWith(u' ');
    setText(text() + (needsSpace ? QStringLiteral(" ") : QString()) + key);
    setFocus();
    end(false);
}

// QLineEdit formats its text only as an input method's: the formats come in
// an input method event that changes nothing else, placed from the cursor.
void HistorySearchField::showTerms()
{
    QList<QInputMethodEvent::Attribute> formats;
    for (const CommitQuery::Span &span : query_.termSpans) {
        QTextCharFormat chip;
        chip.setBackground(chipBackground_);
        chip.setForeground(chipText_);
        formats.append({QInputMethodEvent::TextFormat, int(span.start) - cursorPosition(), int(span.length), chip});
    }
    QInputMethodEvent event(QString(), formats);
    QCoreApplication::sendEvent(this, &event);
}

void HistorySearchField::showError()
{
    const bool hasError = !query_.error.isEmpty();
    // Updated at each keystroke: a style sheet is only set when it changes.
    const QString sheet =
        hasError ? QStringLiteral("QLineEdit { border: 1px solid %1; }").arg(errorColor_.name()) : QString();
    if (sheet != styleSheet())
        setStyleSheet(sheet);
    setToolTip(hasError ? query_.error + QStringLiteral("\n\n") + syntaxHelp() : syntaxHelp());
}
