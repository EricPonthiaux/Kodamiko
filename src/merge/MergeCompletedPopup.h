// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/Integration.h"
#include "ui/OverlayPopup.h"
#include "ui/Theme.h"

class QLabel;
class QPushButton;
class QToolButton;

// Confirmation in the editor after the dedicated merge window has closed.
// The commit action names the exact commit reported by Git's success path.
class MergeCompletedPopup : public OverlayPopup {
    Q_OBJECT

public:
    MergeCompletedPopup(const Theme &theme, QWidget *parent);
    void popup(const Integration::Completion &completion);
    void applyTheme(const Theme &theme);

signals:
    void commitRequested(const QString &id);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    QLabel *icon_;
    QLabel *title_;
    QLabel *description_;
    QLabel *branches_;
    QLabel *commit_;
    QPushButton *resume_;
    QToolButton *close_;
    QString commitId_;
};
