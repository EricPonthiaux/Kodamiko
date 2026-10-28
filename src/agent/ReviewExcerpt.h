// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ReviewReport.h"

#include <QWidget>

// The lines of a file around a finding's line, as the editor shows them:
// their numbers, the code in the code font, and the finding's line tinted
// and underlined with a wave in the color of its severity. Read from the
// disk when created, as the review read it: empty when the file cannot be
// read. Clicks go through it to the widget under it.
class ReviewExcerpt : public QWidget {
    Q_OBJECT

public:
    ReviewExcerpt(const ReviewReport::Finding &finding, QWidget *parent);

    bool isEmpty() const { return lines_.isEmpty(); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Line {
        int number = 0;
        QString text;
    };

    void read(const QString &path);
    int lineHeight() const;

    QList<Line> lines_;
    int findingLine_;
    Diagnostic::Severity severity_;
};
