// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QWidget>

// The header of a file's findings in a list of findings
// (ReviewFindingsList): a caret, the file's folder, muted, and its name,
// then how many errors and warnings it has. A click folds or unfolds the
// file's findings (toggled()).
class ReviewFileHeader : public QWidget {
    Q_OBJECT

public:
    struct Counts {
        qsizetype errors = 0;
        qsizetype warnings = 0;
    };

    // `relativePath` as shown, from the project's folder.
    ReviewFileHeader(const QString &relativePath, Counts counts, QWidget *parent);

    void setExpanded(bool expanded);

    QSize sizeHint() const override;

signals:
    void toggled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void drawCaret(QPainter &painter, QPointF center) const;
    // The counts at the right of `area`: returns where they start.
    qreal drawCounts(QPainter &painter, const QRectF &area) const;

    QString folder_;
    QString fileName_;
    Counts counts_;
    bool isExpanded_ = true;
    bool isHovered_ = false;
};
