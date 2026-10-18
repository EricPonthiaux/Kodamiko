#pragma once

#include "ui/Theme.h"

#include <QFrame>

class DiffView;
class QLabel;
class QToolButton;

// The bar above a DiffView, out of its scrolled text: the file at the top
// of the view as it scrolls (its name, its folder and state, its counts),
// and the buttons that go to the previous or next block of changes.
class DiffHeaderBar : public QFrame {
    Q_OBJECT

public:
    // Follows `diff`, which must outlive it.
    explicit DiffHeaderBar(DiffView *diff, QWidget *parent = nullptr);

    // Shown while the view shows no file.
    void setPlaceholder(const QString &text);
    void applyTheme(const Theme &theme);

private:
    void follow();

    DiffView *diff_;
    QString placeholder_;
    QLabel *name_;
    QLabel *details_;
    QLabel *stats_;
    QToolButton *previous_;
    QToolButton *next_;
    bool dark_ = true;
};
