// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "OverlayPopup.h"
#include "Theme.h"

class PopupHeader;

// About Duet: its mark, which grows in each time the popup opens, its name,
// its version, the author's email addresses (they open the user's mail
// program) and "Made with love" beside a beating heart.
class AboutPopup : public OverlayPopup {
    Q_OBJECT

public:
    AboutPopup(const Theme &theme, QWidget *parent);

    void popup();
    void applyTheme(const Theme &theme);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    class Mark;
    class Heart;

    PopupHeader *header_;
    Mark *mark_;
};
