#pragma once

#include "Prerequisites.h"

#include <QDialog>

class QVBoxLayout;
class QLabel;

// Help ▸ Check Prerequisites, also shown at startup when git or Claude Code
// is missing: the state of each program, and how to install it. The
// installation runs in an editor terminal, so that the user sees what
// happens and can answer prompts (sudo password…).
class PrerequisitesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PrerequisitesDialog(QWidget *parent = nullptr);

    // Checks the programs again and updates the dialog.
    void recheck();
    const QList<Prerequisite> &prerequisites() const { return prerequisites_; }
    bool allOk() const;

signals:
    void installRequested(const QString &title, const ProcessSpec &spec);
    // After each check.
    void checked();

private:
    void rebuild();

    QList<Prerequisite> prerequisites_;
    QLabel *summary_;
    QWidget *rows_;
    QVBoxLayout *rowsLayout_;
};
