// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "OverlayPopup.h"
#include "Theme.h"

class PopupHeader;
class QFrame;

// About Kodamiko: its version, a contact link, a rotating quotation and
// "Made with love" beside a beating heart, in a full-width panel styled
// like Welcome. The content scrolls when the window or font needs it.
class AboutPopup : public OverlayPopup {
    Q_OBJECT

public:
    AboutPopup(const Theme &theme, QWidget *parent);

    void popup();
    void applyTheme(const Theme &theme);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    class Heart;

    QFrame *createIdentity();
    QFrame *createContact();
    QFrame *createDetails();
    QWidget *createBody();

    PopupHeader *header_;
};
