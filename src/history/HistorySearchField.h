// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "CommitQuery.h"
#include "ui/Theme.h"

#include <QLineEdit>

class QAction;

// The history's search field: words and terms, read as a CommitQuery. Its
// filter button inserts each kind of term; its tooltip gives the syntax; a
// term it cannot read turns its border to the error color and is explained
// in the tooltip. The terms it reads ("author:ada", "after:7d") are drawn as
// chips in the accent's colors, so that the query shows how it was read; a
// chip goes whole: Backspace right after it, or "Remove" in the filter
// button's menu.
class HistorySearchField : public QLineEdit {
    Q_OBJECT

public:
    explicit HistorySearchField(QWidget *parent = nullptr);

    // The query as last typed.
    const CommitQuery &query() const { return query_; }

    // Removes term `index` of query().termSpans and a space beside it; the
    // cursor where it was.
    void removeTerm(qsizetype index);
    void applyTheme(const Theme &theme);

signals:
    // At each edit.
    void queryChanged();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void onTextChanged();
    void showTermsMenu();
    // Appends `key` ("author:"…) for its value to be typed.
    void insertTerm(const QString &key);
    void showError();
    // The query's terms in the chips' colors, the rest of the text plain.
    void showTerms();

    CommitQuery query_;
    QAction *searchIcon_;
    QAction *termsButton_;
    QColor errorColor_;
    QColor chipBackground_;
    QColor chipText_;
};
