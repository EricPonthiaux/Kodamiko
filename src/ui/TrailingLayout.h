#pragma once

#include <QLayout>
#include <QList>

// One row of widgets built from the right edge leftwards: the first widget
// added sits rightmost, the next one to its left, and so on. When the row
// runs short of width, the widgets furthest left give up their place first,
// whole: none is ever squeezed, elided or cut. The first widget is the
// anchor: the layout's minimum width keeps room for it.
//
// The widgets can form groups: startGroup() before adding the first one of
// a group. Two shown widgets of different groups are groupSpacing() apart,
// two of the same group spacing() apart.
class TrailingLayout : public QLayout {
public:
    explicit TrailingLayout(QWidget *parent = nullptr);
    ~TrailingLayout() override;
    TrailingLayout(const TrailingLayout &) = delete;
    TrailingLayout &operator=(const TrailingLayout &) = delete;
    TrailingLayout(TrailingLayout &&) = delete;
    TrailingLayout &operator=(TrailingLayout &&) = delete;

    // The widgets added next form a new group.
    void startGroup() { ++currentGroup_; }

    void setGroupSpacing(int spacing) { groupSpacing_ = spacing; }

    int groupSpacing() const { return groupSpacing_; }

    void addItem(QLayoutItem *item) override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    int count() const override;

    QSize sizeHint() const override;
    QSize minimumSize() const override;
    Qt::Orientations expandingDirections() const override;
    void setGeometry(const QRect &rect) override;

private:
    int gap() const;
    // Between the items at `left` and `right`, both shown.
    int gapBetween(qsizetype left, qsizetype right) const;

    QList<QLayoutItem *> items_;
    QList<int> groups_; // the group of each item
    int currentGroup_ = 0;
    int groupSpacing_ = 0;
};
