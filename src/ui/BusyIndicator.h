// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QWidget>

class QLabel;
class Spinner;

// A small spinner and what is being waited for ("Rename “a” to “b”:
// looking for the usages…"), for the status bar. Hidden while nothing is
// waited for.
class BusyIndicator : public QWidget {
    Q_OBJECT

public:
    explicit BusyIndicator(QWidget *parent);

    // Shows `description`, in place of the one shown if any.
    void start(const QString &description);
    void finish();

private:
    Spinner *spinner_;
    QLabel *label_;
};
