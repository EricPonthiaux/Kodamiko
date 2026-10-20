// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "viewers/DiagramKinds.h"
#include "workspace/EntryPopups.h"

#include <functional>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// Asks what Claude should draw: the kind of diagram, a description, and the
// file to write. Ctrl+Enter (or Enter in the name) applies.
class DiagramRequestPopup : public EntryPopup {
    Q_OBJECT

public:
    // `apply` receives the kind's id, the typed name and the description,
    // and returns an error message, or an empty string once the drawing has
    // started: the popup then closes.
    using Apply = std::function<QString(const QString &kindId, const QString &name, const QString &description)>;

    // What the popup offers.
    struct Form {
        QString location;         // the folder, under the title
        QList<DiagramKind> kinds; // to choose from
        QString initialName;      // in the name field
    };

    DiagramRequestPopup(const Form &form, Apply apply, QWidget *parent);

    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void accept() override;
    void focusContent() override;

private:
    Apply apply_;
    QComboBox *kind_;
    QPlainTextEdit *description_;
    QLineEdit *name_;
    QPushButton *draw_;
};
