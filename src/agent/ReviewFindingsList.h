// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewSession.h"

#include <QPointer>
#include <QScrollArea>
#include <QSet>

#include <optional>

class ReviewFindingCard;

// The findings of a review (ReviewSession) grouped by file, each file under
// a header that folds it (ReviewFileHeader), each finding a card
// (ReviewFindingCard), filtered by severity.
//
// It works as a list view: a click selects a finding, Ctrl+click adds one,
// Shift+click a range; Up and Down move the current finding (Shift extends
// the selection), Enter or a double click asks for its place
// (findingRequested()), R resolves and Delete dismisses the selected
// findings, or the current one; the menu resolves, sends or dismisses them.
// A dismissed finding leaves the next one current.
class ReviewFindingsList : public QScrollArea {
    Q_OBJECT

public:
    enum class Filter {
        All,
        Errors,
        Warnings
    };

    explicit ReviewFindingsList(QWidget *parent = nullptr);

    // Follows `session`, not owned; null: none, the list empty.
    void setSession(ReviewSession *session);
    void setFilter(Filter filter);

    // The findings the filter lets through, folded files included, in their
    // order.
    QList<ReviewReport::Finding> shownFindings() const;

    QList<ReviewReport::Finding> selectedFindings() const { return selected_; }

    // Dismisses `findings`; the finding after them becomes the current and
    // selected one.
    void dismiss(const QList<ReviewReport::Finding> &findings);

signals:
    void selectionChanged();
    // Enter, a double click, or a click on a finding's line.
    void findingRequested(const ReviewReport::Finding &finding);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    // The groups and cards anew, the selection and the folded files kept.
    void rebuild();
    void addFile(const QString &path, const QList<ReviewReport::Finding> &findings, QWidget *content);
    ReviewFindingCard *createCard(const ReviewReport::Finding &finding, QWidget *parent);
    void toggleFile(const QString &path);
    // What a click with `modifiers` on `finding` selects.
    void select(const ReviewReport::Finding &finding, Qt::KeyboardModifiers modifiers);
    void selectAll();
    // The card `step` cards from the current one becomes current.
    void moveCurrent(int step, Qt::KeyboardModifiers modifiers);
    // The selection, or the current finding when none is selected.
    QList<ReviewReport::Finding> targets() const;
    void resolve(const QList<ReviewReport::Finding> &findings);
    // The card selected and current flags from the selection.
    void updateCards();
    // Among the cards of the unfolded files; -1 when it has none.
    qsizetype indexOfCard(const ReviewReport::Finding &finding) const;

    QPointer<ReviewSession> session_;
    Filter filter_ = Filter::All;
    QList<ReviewFindingCard *> cards_; // of the unfolded files, in their order
    QSet<QString> foldedPaths_;
    QList<ReviewReport::Finding> selected_;
    QList<ReviewReport::Finding> expanded_;
    std::optional<ReviewReport::Finding> current_;
    std::optional<ReviewReport::Finding> anchor_; // where a Shift selection starts
};
