// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/Theme.h"

#include <QList>
#include <QRect>
#include <QWidget>

class DiffView;

// The files of a DiffView as a strip of chips above it, each its name (and
// its state, for a file shown in two parts) and its added and removed line
// counts, its path in the tooltip; the chip of
// the file at the top of the view stands out, and follows the scroll. A
// click scrolls the view to that file, a double-click asks for the file to
// be opened (fileActivated). The chips wrap on two rows at most; those left
// over go in a last "+N more" chip whose menu lists them. Hidden with fewer
// than two files.
class DiffFileStrip : public QWidget {
    Q_OBJECT

public:
    // Follows `diff`, which must outlive it.
    explicit DiffFileStrip(DiffView *diff, QWidget *parent = nullptr);

    void applyTheme(const Theme &theme);

    // The file whose chip stands out: at the top of the view; -1 without files.
    int currentFile() const { return current_; }

    // Where file `file`'s chip is drawn; an empty rect if it is in the "+N
    // more" chip's menu.
    QRect chipRect(int file) const;

    bool hasHeightForWidth() const override { return true; }

    int heightForWidth(int width) const override;
    QSize sizeHint() const override;

signals:
    // Its path, as the DiffView's file summary says it.
    void fileActivated(const QString &path);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // A chip's place: a file of the view, or the "+N more" chip (file -1).
    struct Chip {
        int file = -1;
        QRect rect;
    };

    // The chips for `width`, two rows at most.
    QList<Chip> layOutChips(int width) const;
    void onContentChanged();
    void followScroll();
    // The chip under `position`; nullptr if none.
    const Chip *chipAt(QPoint position) const;
    void scrollToFile(int file);
    // The files that did not fit, from `first`, in a menu under the "+N more" chip.
    void showMoreMenu(const QRect &chip, int first);
    QString chipText(int file) const;
    QString moreText(int count) const;
    void paintChip(QPainter &painter, const Chip &chip, int hiddenCount) const;

    DiffView *diff_;
    QList<Chip> chips_;
    int current_ = -1;
    QColor background_;
    QColor chipBackground_;
    QColor currentBackground_;
    QColor text_;
    QColor mutedText_;
    QColor added_;
    QColor removed_;
    QColor rule_;
};
