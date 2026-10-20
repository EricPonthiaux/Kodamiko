// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/RepositoryStatus.h"
#include "ui/Theme.h"

#include <QFrame>

class DiffView;
class QLabel;
class QToolButton;

// The bar above a DiffView, out of its scrolled text: the file at the top
// of the view as it scrolls (its name, its folder and state, its counts),
// the buttons that show the two versions side by side and hide whitespace
// changes, and those that go to the previous or next block of changes. While
// file navigation is shown, two arrows go to the previous or next file.
class DiffHeaderBar : public QFrame {
    Q_OBJECT

public:
    // Follows `diff`, which must outlive it.
    explicit DiffHeaderBar(DiffView *diff, QWidget *parent = nullptr);

    // Shown while the view shows no file.
    void setPlaceholder(const QString &text);
    // The previous and next file arrows, for when no file list is in view;
    // hidden at first, and while the view shows a single file.
    void setFileNavigationShown(bool shown);
    void applyTheme(const Theme &theme);

    // Whitespace::Ignore while the whitespace button is checked; Compare at first.
    Whitespace whitespace() const;

    QSize sizeHint() const override;
    int heightForWidth(int width) const override;

signals:
    // The whitespace button was toggled: the diff is to be read again.
    void whitespaceChanged(Whitespace whitespace);
    // Keeps the neighboring splitter rule aligned when a hint wraps.
    void heightChanged(int height);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void follow();
    void updateFileButtons(int file, int fileCount);
    int controlsHeight() const;

    DiffView *diff_;
    QString placeholder_;
    QLabel *name_;
    QLabel *details_;
    QLabel *stats_;
    QToolButton *sideBySide_;
    QToolButton *whitespace_;
    QToolButton *previous_;
    QToolButton *next_;
    QToolButton *previousFile_;
    QToolButton *nextFile_;
    bool isFileNavigationShown_ = false;
    bool dark_ = true;
};
