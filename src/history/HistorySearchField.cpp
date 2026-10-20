#include "HistorySearchField.h"

#include "ui/UiIcons.h"

#include <QMenu>

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
                       "Everything typed is required; contents: and diff: are case sensitive.");
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
    showError();
}

void HistorySearchField::onTextChanged()
{
    query_ = CommitQuery::parse(text());
    showError();
    emit queryChanged();
}

void HistorySearchField::showTermsMenu()
{
    QMenu menu;
    for (const TermKind &kind : termKinds) {
        const QString key = QString::fromLatin1(kind.key);
        menu.addAction(tr(kind.title), this, [this, key] { insertTerm(key); });
    }
    menu.exec(mapToGlobal(QPoint(width() - menu.sizeHint().width(), height())));
}

void HistorySearchField::insertTerm(const QString &key)
{
    const bool needsSpace = !text().isEmpty() && !text().endsWith(u' ');
    setText(text() + (needsSpace ? QStringLiteral(" ") : QString()) + key);
    setFocus();
    end(false);
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
