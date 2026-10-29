// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"

#include <QFrame>
#include <QIcon>

#include <chrono>
#include <functional>
#include <optional>

class QBoxLayout;
class QLabel;
class QPropertyAnimation;
class QPushButton;
class QScrollArea;
class QTextEdit;
class QTimer;
class QToolButton;

// A button of a snackbar message. Running it closes the message.
struct SnackbarAction {
    enum class Emphasis {
        Normal,
        Primary, // in the accent color: the answer the user most likely gives
    };

    QString text;
    std::function<void()> run;
    Emphasis emphasis = Emphasis::Normal;
};

struct SnackbarMessage {
    QIcon icon;
    QString title;
    // Lines under the title, in the code font (a command, a path, a diff).
    // Wrapped without altering the text; long messages scroll in full.
    QString detail;
    QList<SnackbarAction> actions;
    // Closes the message by itself after this long; without one, it stays
    // until an action, its close button or Snackbar::withdraw().
    std::optional<std::chrono::milliseconds> duration;
    // What the close button does, when it is more than closing.
    QString closeToolTip;
    // Run when the close button or the duration closes it; not on an action
    // nor on withdraw().
    std::function<void()> dismissed;
};

// A short message at the bottom of the window, over its contents, in the
// manner of Material's snackbars. The message scrolls when needed; actions
// stay below it and stack when the window is narrow. Posting never takes
// focus: the user keeps typing where they were, and reaches its controls
// with the mouse or Tab. Messages posted while one is shown wait their turn.
//
// It is a child of the window it shows in, and places itself as the window
// is resized.
class Snackbar : public QFrame {
    Q_OBJECT

public:
    using Id = quint64;

    // Shows over `window`'s contents; owned by it.
    explicit Snackbar(QWidget *window);

    // Shows `message`, after the ones waiting. The id withdraws it.
    Id post(SnackbarMessage message);
    // Takes message `id` away, shown or waiting; nothing if it is gone.
    void withdraw(Id id);

    // The message shown and the ones waiting.
    qsizetype count() const { return queue_.size(); }

    // The title of the message shown, empty when there is none.
    QString shownTitle() const;

    void applyTheme(const Theme &theme);

    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Entry {
        Id id = 0;
        SnackbarMessage message;
    };

    void showFirst();
    // Closes the message shown and shows the next, if any.
    void closeFirst();
    void rebuildActions();
    void updateText();
    void updateWaiting();
    int arrangeActions(int width);
    void place();
    void slideIn();

    QList<Entry> queue_;
    Id lastId_ = 0;
    Theme theme_;

    QLabel *icon_;
    QTextEdit *body_;
    QLabel *waiting_;
    QWidget *controls_;
    QScrollArea *footer_;
    QWidget *actionContent_;
    QBoxLayout *actions_;
    QList<QPushButton *> actionButtons_;
    QToolButton *close_;
    QTimer *expiry_;
    QPropertyAnimation *slide_;
    bool placing_ = false;
};
