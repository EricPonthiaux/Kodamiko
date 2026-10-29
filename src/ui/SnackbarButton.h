// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QPushButton>

// A snackbar action whose full label remains readable at a narrow width.
// Keeps QPushButton's shortcuts, accessible text and keyboard behavior.
class SnackbarButton : public QPushButton {
public:
    explicit SnackbarButton(const QString &text, QWidget *parent);

    QSize sizeHint() const override;
    int heightForWidth(int width) const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QSize textPadding() const;
};
