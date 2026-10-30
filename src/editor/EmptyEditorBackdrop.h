// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/Theme.h"

#include <QWidget>

// The editor area while no file is open: only the soft tint of the accent
// that the welcome's brand panel (BrandHero) spreads behind its mark,
// without the mark. Follows the theme; still.
class EmptyEditorBackdrop : public QWidget {
    Q_OBJECT

public:
    EmptyEditorBackdrop(Theme theme, QWidget *parent);

    void applyTheme(const Theme &theme);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Theme theme_;
};
