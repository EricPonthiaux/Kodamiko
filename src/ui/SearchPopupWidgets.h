// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QStyledItemDelegate>

class QLabel;
class QLineEdit;
struct Theme;

// Shared presentation of the file picker, command palette and project search.
namespace SearchPopup {

QString styleSheet(const Theme &theme);
// Compact search row, with room for trailing search options.
QWidget *searchArea(QLineEdit *field, QWidget *parent);
// Keyboard hints on the left, result count on the right.
QWidget *footer(const QList<QPair<QString, QString>> &hints, QLabel *summary, QWidget *parent);
// Centered message, shown in place of an empty result list.
QLabel *emptyState(QWidget *parent);
// A quiet result count in the footer; the caller supplies its text.
QLabel *resultSummary(QWidget *parent);

// One-line result: primary text followed by muted context and an optional icon.
class ResultDelegate : public QStyledItemDelegate {
public:
    static constexpr int detailRole = Qt::UserRole + 20;
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

} // namespace SearchPopup
