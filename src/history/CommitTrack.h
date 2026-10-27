// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/CommitLog.h"
#include "ui/Theme.h"

#include <QWidget>

// A file's commits on a line of time, the oldest at the left: each a dot in
// the color of its age, under a bar as high as the share of the file's lines
// it still wrote; a commit made with Claude rings its dot in Claude's color.
// A click on a commit chooses it (commitClicked); its tooltip names it.
class CommitTrack : public QWidget {
    Q_OBJECT

public:
    struct Mark {
        CommitSummary commit;
        int lineCount = 0; // the file's lines it still wrote
        QColor ageColor;
        bool isMadeWithClaude = false;
    };

    explicit CommitTrack(QWidget *parent = nullptr);

    // In any order.
    void setMarks(QList<Mark> marks);
    // Fills the dot of commit `id`; empty: none.
    void setChosen(const QString &id);
    void applyTheme(const Theme &theme);

    QSize sizeHint() const override;

signals:
    void commitClicked(const QString &id);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    // The mark at `position`, the nearest within reach; nullptr if none.
    const Mark *markAt(QPointF position) const;
    qreal xOf(const QDateTime &date) const;
    void paintMark(QPainter &painter, const Mark &mark, int highestCount) const;
    // The day under the dots, where it does not overlap the one before.
    void paintDays(QPainter &painter) const;

    QList<Mark> marks_; // oldest first
    QString chosen_;
    Theme theme_;
};
