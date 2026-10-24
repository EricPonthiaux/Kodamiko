// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QFrame>

class QHBoxLayout;
class QLabel;
class QPushButton;

// The footer of a popup built like a dialog: a line above it, a hint on the
// left in muted text, the buttons on the right, the default one last, as
// in IntelliJ's dialogs. Its rules come with popupStyleSheet().
class PopupFooter : public QFrame {
    Q_OBJECT

public:
    explicit PopupFooter(QWidget *parent);

    // The hint, on one line.
    void setHint(const QString &text);
    // The label of the hint, for a hint of several lines (Paragraph) or
    // one that follows a form. Owned by the footer.
    QLabel *hint() const { return hint_; }

    // On the right, after the buttons already added, at least
    // PopupMetrics::buttonMinWidth wide. The footer takes it as a child.
    void addButton(QPushButton *button);
    // Set apart on the left, before the hint: an action that destroys
    // something, kept away from the default button.
    void addLeadingButton(QPushButton *button);

private:
    QHBoxLayout *layout_;
    QLabel *hint_;
};
