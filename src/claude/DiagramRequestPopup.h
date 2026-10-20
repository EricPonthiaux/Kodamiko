// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/OverlayPopup.h"
#include "ui/Theme.h"
#include "viewers/DiagramKinds.h"

#include <functional>

class PopupHeader;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// Claude ▸ Draw a Diagram, a popup over the window: the kind of diagram, a
// description of what Claude should draw, and the file to write.
// Ctrl+Enter (or Enter outside the description) draws.
class DiagramRequestPopup : public OverlayPopup {
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

    DiagramRequestPopup(const Theme &theme, QWidget *parent);

    // Shows the popup for `form`, the description emptied; `apply` is called
    // when the user draws.
    void popup(const Form &form, Apply apply);
    void applyTheme(const Theme &theme);

    // Hands the fields to `apply_`: closes on success, shows its error
    // otherwise.
    void draw();

    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void showError(const QString &message);

    Apply apply_;
    PopupHeader *header_;
    QComboBox *kind_;
    QPlainTextEdit *description_;
    QLineEdit *name_;
    QLabel *error_;
    QPushButton *draw_;
};
