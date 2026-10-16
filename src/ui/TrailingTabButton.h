#pragma once

#include <QObject>

class QAbstractButton;
class QTabWidget;

// Places `button` (a "+" to add a tab) right after the last tab of `tabs`,
// following the tabs as they are added, closed or renamed. When the tabs
// fill the bar, it stays at the bar's right end, in a space kept for it.
class TrailingTabButton : public QObject {
    Q_OBJECT

public:
    TrailingTabButton(QTabWidget *tabs, QAbstractButton *button);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void place();

    QTabWidget *tabs_;
    QAbstractButton *button_;
    QWidget *room_;
    bool placing_ = false;
};
