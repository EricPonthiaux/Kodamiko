// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "workspace/EntryPopups.h"

#include <functional>

class QCheckBox;
class QLineEdit;

// Asks how to put the changes aside: an optional message, and whether new
// files go too. Enter or the Stash button applies.
class StashChangesPopup : public EntryPopup {
    Q_OBJECT

public:
    // `stash` receives the message (trimmed, maybe empty) and whether new
    // files are included.
    StashChangesPopup(const QString &location,
                      std::function<void(const QString &message, bool includeUntracked)> stash,
                      QWidget *parent);

protected:
    void accept() override;
    void focusContent() override;

private:
    std::function<void(const QString &, bool)> stash_;
    QLineEdit *message_;
    QCheckBox *untracked_;
};
