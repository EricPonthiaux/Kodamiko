// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/RepositoryStatus.h"
#include "ui/Theme.h"

#include <QFrame>

class DiffView;
class QHBoxLayout;
class QLabel;
class QToolButton;

// The bar above a DiffView, out of its scrolled text: the file at the top
// of the view as it scrolls (its name, its folder and state, its counts),
// the buttons that show the two versions side by side and hide whitespace
// changes, and those that go to the previous or next block of changes. With
// several files, a pager ("‹ 3 / 12 ›") goes to the previous or next file.
class DiffHeaderBar : public QFrame {
    Q_OBJECT

public:
    // Follows `diff`, which must outlive it.
    explicit DiffHeaderBar(DiffView *diff, QWidget *parent = nullptr);

    // Shown while the view shows no file.
    void setPlaceholder(const QString &text);
    void applyTheme(const Theme &theme);

    // Whitespace::Ignore while the whitespace button is checked; Compare at first.
    Whitespace whitespace() const;
    // The height of its buttons and margins: its height unless the hint wraps.
    int controlsHeight() const;

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
    QFrame *createFilePager();
    void updateFilePager(int file, int fileCount);

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
    QFrame *filePager_;
    QLabel *filePosition_;
    QHBoxLayout *layout_;
    QWidget *buttonsSide_;
    bool dark_ = true;
};
