#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

class QScrollBar;

// Marks drawn over a vertical scroll bar, to show where things are in the
// whole document (the occurrences of a name…). An overlay rather than a
// scroll bar subclass: QScintilla keeps its own scroll bar and its
// connections. Clicks go through to the scroll bar.
class ScrollBarMarks : public QWidget {
    Q_OBJECT

public:
    // Over `scrollBar`, which owns it.
    explicit ScrollBarMarks(QScrollBar *scrollBar);

    // Marks at `positions`, from 0 (the document's start) to 1 (its end).
    void setMarks(const QList<qreal> &positions, const QColor &color);
    void clear();

    const QList<qreal> &marks() const { return positions_; }

    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QScrollBar *scrollBar_;
    QList<qreal> positions_;
    QColor color_;
};
