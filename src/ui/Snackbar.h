// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"

#include <QFrame>
#include <QIcon>

#include <chrono>
#include <functional>
#include <optional>

class QHBoxLayout;
class QLabel;
class QPushButton;
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
    // A line under the title, in the code font (a command, a path); elided,
    // shown in full in its tooltip.
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
// manner of Material's snackbars: an icon, a title, a detail line, a few
// buttons and a close button. It never takes the focus: the user keeps
// typing where they were, and reaches its buttons with the mouse or Tab.
// The messages posted while one is shown wait their turn, and their count
// is shown.
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
    // The title and the detail of the message shown, elided to their room.
    void fitTexts();
    void place();
    // Where it goes, shadow included, at the bottom center of the window.
    QRect placedGeometry() const;
    void slideIn();

    QList<Entry> queue_;
    Id lastId_ = 0;
    Theme theme_;

    QLabel *icon_;
    QLabel *title_;
    QLabel *detail_;
    QLabel *waiting_;
    QHBoxLayout *actions_;
    QList<QPushButton *> actionButtons_;
    QToolButton *close_;
    QTimer *expiry_;
};
