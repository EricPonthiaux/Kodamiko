// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "workspace/EntryPopups.h"

#include <functional>

class QCheckBox;
class QLineEdit;

// Asks for the new name of a symbol, as IntelliJ's Rename does: Refactor
// (Enter) renames at once, Preview (Alt+P) first shows what changes; the
// name in comments and strings is renamed too while that box is checked
// (kept from a rename to the next). The popup closes once the name is
// accepted, and deletes itself.
class RenameSymbolPopup : public EntryPopup {
    Q_OBJECT

public:
    enum class Mode {
        Refactor,
        Preview
    };

    struct Choice {
        QString newName; // trimmed
        Mode mode = Mode::Refactor;
        bool inCommentsAndStrings = true;
    };

    // Returns an error message, or an empty string once done.
    using Rename = std::function<QString(const Choice &choice)>;

    // `name` is the current one, selected in the field; `location` says
    // where the symbol is (its file).
    RenameSymbolPopup(const QString &name, const QString &location, Rename rename, QWidget *parent);

    QLineEdit *field() const { return field_; }

    QCheckBox *commentsAndStrings() const { return commentsAndStrings_; }

    // Preview, as its button.
    void preview();

protected:
    void accept() override;
    void focusContent() override;

private:
    void submit(Mode mode);

    Rename rename_;
    QLineEdit *field_;
    QCheckBox *commentsAndStrings_;
};
