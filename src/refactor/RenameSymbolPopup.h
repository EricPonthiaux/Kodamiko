// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "workspace/EntryPopups.h"

#include <functional>

class QLineEdit;

// Asks for the new name of a symbol, as IntelliJ's Rename does: Refactor
// (Enter) renames at once, Preview (Alt+P) first shows what changes. The
// popup closes once the name is accepted, and deletes itself.
class RenameSymbolPopup : public EntryPopup {
    Q_OBJECT

public:
    enum class Mode {
        Refactor,
        Preview
    };

    // Receives the new name, trimmed; returns an error message, or an
    // empty string once done.
    using Rename = std::function<QString(const QString &newName, Mode mode)>;

    // `name` is the current one, selected in the field; `location` says
    // where the symbol is (its file).
    RenameSymbolPopup(const QString &name, const QString &location, Rename rename, QWidget *parent);

    QLineEdit *field() const { return field_; }

    // Preview, as its button.
    void preview();

protected:
    void accept() override;
    void focusContent() override;

private:
    void submit(Mode mode);

    Rename rename_;
    QLineEdit *field_;
};
