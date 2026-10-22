// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QToolButton>

// A bounded label that yields space to adjacent controls. Only the painted
// text is elided: menus, accessibility and callers retain the full text.
// Its bar measures it at boundedWidth(), then hands it the room left over
// with widenInto(), so the label is only shortened when the bar is full.
class ElidingToolButton : public QToolButton {
    Q_OBJECT

public:
    explicit ElidingToolButton(QWidget *parent = nullptr, int preferredWidth = 240);

    // A dropdown chevron, for example; kept visible after the elided label.
    void setTextSuffix(const QString &suffix);

    // The width it claims while its neighbors compete for room: the whole
    // label, up to the preferred width given at construction.
    int boundedWidth() const;
    // Grows past boundedWidth() by up to `sparePx`, as far as the whole label
    // needs; returns the pixels taken.
    int widenInto(int sparePx);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int fullWidth() const;

    int preferredWidth_;
    int extraWidth_ = 0;
    QString suffix_;
};
