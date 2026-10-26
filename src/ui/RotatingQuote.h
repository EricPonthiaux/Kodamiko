// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QWidget>

class QLabel;
class QTimer;

// A quotation about writing software and its author, replaced by the next
// one every few seconds while the widget is shown. Each showing starts on a
// random quotation; the change fades in when motion is on.
class RotatingQuote : public QWidget {
    Q_OBJECT

public:
    explicit RotatingQuote(QWidget *parent);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void showQuote(qsizetype index);
    void showNextQuote();

    QLabel *text_;
    QLabel *author_;
    QTimer *rotation_;
    qsizetype index_ = 0;
};
