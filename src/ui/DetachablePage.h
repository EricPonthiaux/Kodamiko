// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"

#include <QList>
#include <QObject>
#include <QString>

#include <memory>

class QAction;
class QLabel;
class QStackedWidget;
class QToolButton;
class WindowFrame;
class WindowTitleBar;

// A page of a QStackedWidget that can leave for a window of its own, to be
// put on another screen, and come back. While it is away, a placeholder
// takes its place in the stack, with buttons to show the window or bring
// the page back; closing the window brings it back too. The window is
// independent (not kept above the main one); its geometry is remembered
// under a settings key. The page always starts in the stack.
class DetachablePage : public QObject {
    Q_OBJECT

public:
    struct Texts {
        QString windowTitle;  // "Terminals — project"
        QString awayMessage;  // what the placeholder says
        QString attachAction; // "Bring the Terminals Back"
    };

    // `page` must be in `stack`, which owns this object; `settingsKey`
    // names where its state is saved.
    DetachablePage(QStackedWidget *stack, QWidget *page, QString settingsKey, Texts texts);
    ~DetachablePage() override;
    Q_DISABLE_COPY_MOVE(DetachablePage)

    bool isDetached() const { return window_ != nullptr; }

    // Moves the page into its window, where it was last (or over the stack).
    void detach();
    // Brings the page back to its place in the stack.
    void attach();
    // Brings the window to the front, activated; nothing while attached.
    void raiseWindow();
    void setWindowTitle(const QString &title);
    // Frameless: Kodamiko's frame and title bar around the window, as around
    // the main one; otherwise the system's.
    void setFrameless(bool frameless);
    void applyTheme(const Theme &theme);
    // Actions whose shortcuts also work in the window: the main window's
    // menu commands.
    void setWindowActions(const QList<QAction *> &actions);

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void detachedChanged(bool detached);
    // The window was activated or deactivated.
    void windowActivationChanged();

private:
    QWidget *createPlaceholder();
    void saveState() const;

    QStackedWidget *stack_;
    QWidget *page_;
    QString settingsKey_;
    Texts texts_;
    QWidget *placeholder_; // in the stack while the page is away
    std::unique_ptr<QWidget> window_;
    WindowFrame *frame_ = nullptr;       // of window_
    WindowTitleBar *titleBar_ = nullptr; // of window_
    bool frameless_ = false;
    Theme theme_;
    QList<QAction *> windowActions_;
};
