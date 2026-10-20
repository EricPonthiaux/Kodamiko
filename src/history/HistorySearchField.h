#pragma once

#include "CommitQuery.h"
#include "ui/Theme.h"

#include <QLineEdit>

class QAction;

// The history's search field: words and terms, read as a CommitQuery. Its
// filter button inserts each kind of term; its tooltip gives the syntax; a
// term it cannot read turns its border to the error color and is explained
// in the tooltip.
class HistorySearchField : public QLineEdit {
    Q_OBJECT

public:
    explicit HistorySearchField(QWidget *parent = nullptr);

    // The query as last typed.
    const CommitQuery &query() const { return query_; }

    void applyTheme(const Theme &theme);

signals:
    // At each edit.
    void queryChanged();

private:
    void onTextChanged();
    void showTermsMenu();
    // Appends `key` ("author:"…) for its value to be typed.
    void insertTerm(const QString &key);
    void showError();

    CommitQuery query_;
    QAction *searchIcon_;
    QAction *termsButton_;
    QColor errorColor_;
};
