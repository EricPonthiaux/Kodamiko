// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Prerequisites.h"
#include "ui/OverlayPopup.h"
#include "ui/Theme.h"

class PopupHeader;
class QFrame;
class QHBoxLayout;
class QVBoxLayout;

// Help ▸ Check Prerequisites, also shown at startup when git or Claude Code
// is missing: a popup over the window, like Find in Files, with the state
// of each program and how to install it. The installation runs in an
// editor terminal, so that the user sees what happens and can answer
// prompts (sudo password…).
class PrerequisitesPopup : public OverlayPopup {
    Q_OBJECT

public:
    PrerequisitesPopup(const Theme &theme, QWidget *parent);

    // Checks the programs again and shows the popup.
    void popup();
    // Checks the programs again and updates the popup.
    void recheck();
    void applyTheme(const Theme &theme);

    const QList<Prerequisite> &prerequisites() const { return prerequisites_; }

    bool allOk() const;

signals:
    // The popup hides itself, so that the terminal can be seen.
    void installRequested(const QString &title, const ProcessSpec &spec);
    // After each check.
    void checked();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void rebuild();
    QFrame *createSummary();
    QWidget *createFooter();
    // A program's state and, unless it is fine, how to install it.
    QFrame *createCard(const Prerequisite &prerequisite);
    // The install command, with buttons to copy and run it.
    QHBoxLayout *createInstallRow(const Prerequisite &prerequisite, QWidget *card);

    Theme theme_;
    QList<Prerequisite> prerequisites_;
    PopupHeader *header_;
    QVBoxLayout *cardsLayout_;
};
