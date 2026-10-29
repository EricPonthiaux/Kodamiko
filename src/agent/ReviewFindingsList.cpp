// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewFindingsList.h"

#include "ReviewFileHeader.h"
#include "ReviewFindingCard.h"
#include "ui/ContextMenus.h"

#include <QContextMenuEvent>
#include <QDir>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int groupSpacingPx = 2;
constexpr int cardSpacingPx = 10;
// The cards under their file's name, past its caret.
constexpr int cardIndentPx = 0;
constexpr int groupBottomPx = 10;
constexpr int visibleMarginPx = 8;

using ReviewReport::Finding;

QString emptyText(ReviewFindingsList::Filter filter)
{
    switch (filter) {
    case ReviewFindingsList::Filter::All:
        return ReviewFindingsList::tr("All findings have been dismissed.\nThe code has not been reviewed again.");
    case ReviewFindingsList::Filter::Errors:
        return ReviewFindingsList::tr("No errors left.");
    case ReviewFindingsList::Filter::Warnings:
        return ReviewFindingsList::tr("No warnings left.");
    }
    return {};
}

bool isLetThrough(const Finding &finding, ReviewFindingsList::Filter filter)
{
    switch (filter) {
    case ReviewFindingsList::Filter::All:
        return true;
    case ReviewFindingsList::Filter::Errors:
        return finding.severity == Diagnostic::Severity::Error;
    case ReviewFindingsList::Filter::Warnings:
        return finding.severity == Diagnostic::Severity::Warning;
    }
    return true;
}

ReviewFileHeader::Counts countsOf(const QList<Finding> &findings)
{
    const auto isError = [](const Finding &finding) { return finding.severity == Diagnostic::Severity::Error; };
    const qsizetype errors = std::ranges::count_if(findings, isError);
    return {.errors = errors, .warnings = findings.size() - errors};
}

} // namespace

ReviewFindingsList::ReviewFindingsList(QWidget *parent)
    : QScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFocusPolicy(Qt::StrongFocus);
    rebuild();
}

void ReviewFindingsList::setSession(ReviewSession *session)
{
    if (session_)
        session_->disconnect(this);
    session_ = session;
    selected_.clear();
    expanded_.clear();
    current_.reset();
    anchor_.reset();
    foldedPaths_.clear();
    if (session) {
        connect(session, &ReviewSession::stateChanged, this, &ReviewFindingsList::rebuild);
        connect(session, &ReviewSession::findingsChanged, this, &ReviewFindingsList::rebuild);
    }
    rebuild();
}

void ReviewFindingsList::setFilter(Filter filter)
{
    filter_ = filter;
    selected_.clear();
    anchor_.reset();
    rebuild();
}

QList<Finding> ReviewFindingsList::shownFindings() const
{
    QList<Finding> shown;
    if (session_)
        std::ranges::copy_if(session_->findings(), std::back_inserter(shown),
                             [this](const Finding &finding) { return isLetThrough(finding, filter_); });
    return shown;
}

void ReviewFindingsList::rebuild()
{
    const int scroll = verticalScrollBar()->value();
    cards_.clear();
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(groupSpacingPx);
    const QList<Finding> shown = shownFindings();
    QStringList paths;
    for (const Finding &finding : shown) {
        if (!paths.contains(finding.path))
            paths.append(finding.path);
    }
    for (const QString &path : std::as_const(paths)) {
        QList<Finding> ofFile;
        std::ranges::copy_if(shown, std::back_inserter(ofFile),
                             [&path](const Finding &finding) { return finding.path == path; });
        addFile(path, ofFile, content);
    }
    if (session_ && shown.isEmpty()) {
        auto *empty = new QLabel(emptyText(filter_), content);
        empty->setForegroundRole(QPalette::PlaceholderText);
        empty->setAlignment(Qt::AlignCenter);
        layout->addWidget(empty);
    }
    layout->addStretch();
    // Deletes the previous content: the cards signal it queued, never from
    // inside their own handlers.
    setWidget(content);

    selected_.removeIf([this](const Finding &finding) { return indexOfCard(finding) < 0; });
    if (!current_ || indexOfCard(*current_) < 0)
        current_ = cards_.isEmpty() ? std::nullopt : std::optional(cards_.first()->finding());
    updateCards();
    // Once the new content is laid out.
    QTimer::singleShot(0, this, [this, scroll] { verticalScrollBar()->setValue(scroll); });
    emit selectionChanged();
}

void ReviewFindingsList::addFile(const QString &path, const QList<Finding> &findings, QWidget *content)
{
    const bool isFolded = foldedPaths_.contains(path);
    auto *header = new ReviewFileHeader(QDir(session_->workDir()).relativeFilePath(path), countsOf(findings), content);
    header->setExpanded(!isFolded);
    connect(header, &ReviewFileHeader::toggled, this, [this, path] { toggleFile(path); }, Qt::QueuedConnection);
    content->layout()->addWidget(header);
    if (isFolded)
        return;
    auto *cards = new QWidget(content);
    auto *layout = new QVBoxLayout(cards);
    layout->setContentsMargins(cardIndentPx, 0, 0, groupBottomPx);
    layout->setSpacing(cardSpacingPx);
    for (const Finding &finding : findings)
        layout->addWidget(createCard(finding, cards));
    content->layout()->addWidget(cards);
}

ReviewFindingCard *ReviewFindingsList::createCard(const Finding &finding, QWidget *parent)
{
    const auto status =
        session_->isResolving(finding) ? ReviewFindingCard::Status::Sent : ReviewFindingCard::Status::Open;
    auto *card = new ReviewFindingCard(finding, status, session_->agentName(), parent);
    card->setExpanded(expanded_.contains(finding));
    connect(card, &ReviewFindingCard::detailsToggled, this, [this, finding](bool expanded) {
        expanded_.removeOne(finding);
        if (expanded)
            expanded_.append(finding);
    });
    cards_.append(card);
    connect(card, &ReviewFindingCard::pressed, this, [this, finding](Qt::KeyboardModifiers modifiers) {
        setFocus(Qt::MouseFocusReason);
        select(finding, modifiers);
    });
    connect(card, &ReviewFindingCard::activated, this, [this, finding] { emit findingRequested(finding); });
    connect(card, &ReviewFindingCard::sendRequested, this, [this, finding] {
        if (session_)
            session_->send({finding});
    });
    // Queued: resolving and dismissing rebuild the cards, this one among them.
    connect(
        card, &ReviewFindingCard::resolveRequested, this, [this, finding] { resolve({finding}); },
        Qt::QueuedConnection);
    connect(
        card, &ReviewFindingCard::dismissRequested, this, [this, finding] { dismiss({finding}); },
        Qt::QueuedConnection);
    return card;
}

void ReviewFindingsList::toggleFile(const QString &path)
{
    if (!foldedPaths_.remove(path))
        foldedPaths_.insert(path);
    rebuild();
}

void ReviewFindingsList::select(const Finding &finding, Qt::KeyboardModifiers modifiers)
{
    const bool extends = modifiers.testFlag(Qt::ShiftModifier) && anchor_ && indexOfCard(*anchor_) >= 0;
    if (extends) {
        const auto [first, last] = std::minmax({indexOfCard(*anchor_), indexOfCard(finding)});
        selected_.clear();
        for (qsizetype i = first; i <= last; ++i)
            selected_.append(cards_.at(i)->finding());
    } else if (modifiers.testFlag(Qt::ControlModifier)) {
        if (!selected_.removeOne(finding))
            selected_.append(finding);
        anchor_ = finding;
    } else {
        selected_ = {finding};
        anchor_ = finding;
    }
    current_ = finding;
    updateCards();
    emit selectionChanged();
}

void ReviewFindingsList::selectAll()
{
    selected_.clear();
    for (const ReviewFindingCard *card : std::as_const(cards_))
        selected_.append(card->finding());
    updateCards();
    emit selectionChanged();
}

void ReviewFindingsList::moveCurrent(int step, Qt::KeyboardModifiers modifiers)
{
    if (cards_.isEmpty())
        return;
    const qsizetype index = current_ ? indexOfCard(*current_) : -1;
    ReviewFindingCard *card = cards_.at(std::clamp<qsizetype>(index + step, 0, cards_.size() - 1));
    // Shift extends the selection; Ctrl does not apply to a move.
    select(card->finding(), modifiers & Qt::ShiftModifier);
    ensureWidgetVisible(card, 0, visibleMarginPx);
}

QList<Finding> ReviewFindingsList::targets() const
{
    if (!selected_.isEmpty())
        return selected_;
    if (current_)
        return {*current_};
    return {};
}

void ReviewFindingsList::resolve(const QList<Finding> &findings)
{
    if (session_)
        session_->resolve(findings);
}

void ReviewFindingsList::dismiss(const QList<Finding> &findings)
{
    if (!session_ || findings.isEmpty())
        return;
    // The first card left after the last one dismissed, or else before it.
    qsizetype last = -1;
    for (const Finding &finding : findings)
        last = std::max(last, indexOfCard(finding));
    std::optional<Finding> next;
    for (qsizetype i = last + 1; i < cards_.size() && !next; ++i) {
        if (!findings.contains(cards_.at(i)->finding()))
            next = cards_.at(i)->finding();
    }
    for (qsizetype i = last - 1; i >= 0 && !next; --i) {
        if (!findings.contains(cards_.at(i)->finding()))
            next = cards_.at(i)->finding();
    }
    current_ = next;
    anchor_ = next;
    selected_ = next ? QList<Finding>{*next} : QList<Finding>{};
    session_->dismiss(findings);
}

void ReviewFindingsList::keyPressEvent(QKeyEvent *event)
{
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    switch (event->key()) {
    case Qt::Key_Up:
        moveCurrent(-1, modifiers);
        return;
    case Qt::Key_Down:
        moveCurrent(1, modifiers);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (current_)
            emit findingRequested(*current_);
        return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        dismiss(targets());
        return;
    case Qt::Key_R:
        if (modifiers == Qt::NoModifier)
            resolve(targets());
        return;
    case Qt::Key_A:
        if (modifiers == Qt::ControlModifier)
            selectAll();
        return;
    default:
        QScrollArea::keyPressEvent(event);
    }
}

void ReviewFindingsList::focusInEvent(QFocusEvent *event)
{
    QScrollArea::focusInEvent(event);
    updateCards();
}

void ReviewFindingsList::focusOutEvent(QFocusEvent *event)
{
    QScrollArea::focusOutEvent(event);
    updateCards();
}

void ReviewFindingsList::contextMenuEvent(QContextMenuEvent *event)
{
    if (!session_)
        return;
    QWidget *clicked = widget()->childAt(widget()->mapFromGlobal(event->globalPos()));
    while (clicked && !qobject_cast<ReviewFindingCard *>(clicked))
        clicked = clicked->parentWidget();
    if (const auto *card = qobject_cast<ReviewFindingCard *>(clicked); card && !selected_.contains(card->finding()))
        select(card->finding(), Qt::NoModifier);
    const QList<Finding> findings = targets();
    if (findings.isEmpty())
        return;
    // Resolve and Send leave out the findings handed over to be fixed.
    const bool canHandOver = !session_->notHandedOver(findings).isEmpty();
    QMenu menu(this);
    QAction *resolveAction = menu.addAction(tr("Resolve") + QLatin1Char('\t') + QStringLiteral("R"));
    resolveAction->setEnabled(canHandOver);
    connect(resolveAction, &QAction::triggered, this, [this, findings] { resolve(findings); });
    QAction *sendAction = menu.addAction(tr("Send to %1").arg(session_->agentName()));
    sendAction->setEnabled(canHandOver);
    connect(sendAction, &QAction::triggered, this, [this, findings] { session_->send(findings); });
    menu.addSeparator();
    const QString dismissKey = QKeySequence(QKeySequence::Delete).toString(QKeySequence::NativeText);
    connect(menu.addAction(tr("Dismiss") + QLatin1Char('\t') + dismissKey), &QAction::triggered, this,
            [this, findings] { dismiss(findings); });
    ContextMenus::exec(menu, *this, event->globalPos());
}

void ReviewFindingsList::updateCards()
{
    for (ReviewFindingCard *card : std::as_const(cards_)) {
        card->setSelected(selected_.contains(card->finding()));
        card->setCurrent(hasFocus() && current_ == card->finding());
    }
}

qsizetype ReviewFindingsList::indexOfCard(const Finding &finding) const
{
    const auto found =
        std::ranges::find_if(cards_, [&finding](const ReviewFindingCard *card) { return card->finding() == finding; });
    return found == cards_.end() ? -1 : found - cards_.begin();
}
