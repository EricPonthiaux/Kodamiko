// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/GitOperations.h"
#include "workspace/EntryPopups.h"

#include <functional>

class QCheckBox;
class QLineEdit;

// Asks who commits when git does not know: the name and email of the
// commits, for every repository or this one only. Enter or Save applies.
class IdentityPopup : public EntryPopup {
    Q_OBJECT

public:
    // `current` fills the fields (git's configuration, maybe partial).
    // `save` receives the identity, both parts trimmed and set.
    IdentityPopup(const QString &location,
                  const GitOperations::Identity &current,
                  std::function<void(const GitOperations::Identity &, GitOperations::ConfigScope)> save,
                  QWidget *parent);

    QLineEdit *nameField() const { return name_; }

    QLineEdit *emailField() const { return email_; }

protected:
    void accept() override;
    void focusContent() override;

private:
    std::function<void(const GitOperations::Identity &, GitOperations::ConfigScope)> save_;
    QLineEdit *name_;
    QLineEdit *email_;
    QCheckBox *repositoryOnly_;
};
