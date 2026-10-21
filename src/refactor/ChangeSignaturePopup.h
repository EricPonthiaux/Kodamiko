// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ChangeSignature.h"
#include "ui/OverlayPopup.h"

#include <functional>

class PopupHeader;
class QLabel;
class QPushButton;
class QTableWidget;

// Change Signature's popup, as IntelliJ's: the function's parameters in a
// table, which Add, Remove, Move Up and Move Down change; a new
// parameter's declaration and the value its calls pass are typed in its
// row. The new signature shows under the table. Refactor hands the new
// parameters over (to show the changes before they are made), Cancel or
// Escape drops them. It follows the applied theme and deletes itself once
// dismissed.
class ChangeSignaturePopup : public OverlayPopup {
    Q_OBJECT

public:
    using Accept = std::function<void(const QList<ChangeSignature::Parameter> &parameters)>;

    ChangeSignaturePopup(QString languageId, ChangeSignature::Signature signature, Accept accept, QWidget *parent);

    void popup();

    // The buttons' commands.
    void addParameter();
    void removeParameter();
    void moveUp();
    void moveDown();
    // Refactor: hands the parameters over, unless one is incomplete.
    void accept();

    // The parameters as the table has them.
    QList<ChangeSignature::Parameter> parameters() const;

    QTableWidget *table() const { return table_; }

    QLabel *error() const { return error_; }

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void addRow(int row, const ChangeSignature::Parameter &parameter, const QString &declaration);
    void moveBy(int rows);
    void updateSignature();

    QString languageId_;
    ChangeSignature::Signature signature_;
    Accept accept_;

    PopupHeader *header_;
    QTableWidget *table_;
    QLabel *preview_;
    QLabel *error_;
    QPushButton *refactor_;
};
