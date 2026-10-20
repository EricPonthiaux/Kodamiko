// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/OverlayPopup.h"

#include <functional>

class PopupHeader;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

// Where a name typed in a popup leads: the absolute path to create or
// rename to, or why it cannot be used.
struct EntryTarget {
    QString path;
    QString error;
};

// Checks `name` for a new file or folder in `folder`. Subfolders may be
// given in the name ("src/new/file.cpp"); the result must stay within
// `root` and not exist yet.
EntryTarget resolveNewEntry(const QString &root, const QString &folder, const QString &name, bool isFolder);

// Checks `name` as the new name of `path`, in the same folder. The same
// name gives `path` back; a change of case only is accepted.
EntryTarget resolveRename(const QString &path, const QString &name);

// Small popup over the window for an operation on a file, a folder, a
// branch…, built like the other popups (OverlayPopup): a header with an
// icon, the title and the location, then the content, the errors in red,
// and a footer with a hint and the buttons. It follows the theme applied
// (ThemeStyle::applied()); its height follows its content. Enter applies,
// Escape, Cancel or the header's close button cancels. It deletes itself
// once dismissed.
class EntryPopup : public OverlayPopup {
    Q_OBJECT

public:
    // Shows the popup centered in the parent's window.
    void popup();
    // Shows the popup and returns once it closed, its answer applied and
    // the popup deleted: for a caller that needs the answer before going on.
    // Events are processed meanwhile.
    void exec();
    // One of UiIcons', in the header.
    void setIconName(const QString &iconName);

    QString error() const;
    void showError(const QString &message);

protected:
    // `settingsKey` keeps the width the user gives this kind of popup.
    EntryPopup(const QString &settingsKey, const QString &title, const QString &location, QWidget *parent);

    void keyPressEvent(QKeyEvent *event) override;
    virtual void accept() = 0;
    virtual void focusContent() = 0;

    QVBoxLayout *content() const { return content_; }

    // A button of the footer, right of those added before.
    void addFooterButton(QPushButton *button);
    // The hint left of the footer's buttons.
    void setFooterHint(const QString &hint);

    // Recomputes the height at the current width, including wrapped text.
    void resizeToContent();

    // Called when the error appears or disappears.
    virtual void errorChanged(bool shown) { Q_UNUSED(shown); }

private:
    QString location_;
    PopupHeader *header_;
    QVBoxLayout *content_;
    QLabel *error_;
    QLabel *hint_;
    QHBoxLayout *footerButtons_;
};

// Asks for a name: a single field, outlined in red while an error is
// shown; the error disappears as soon as the name is edited.
class NamePopup : public EntryPopup {
    Q_OBJECT

public:
    // `apply` receives the typed name (trimmed) and returns an error
    // message, or an empty string once done: the popup then closes.
    NamePopup(const QString &title,
              const QString &location,
              std::function<QString(const QString &)> apply,
              QWidget *parent);

    // Initial name; for a file, the part before the extension is selected,
    // as when renaming in IntelliJ.
    void setName(const QString &name, bool isFolder);

    QLineEdit *field() const { return field_; }

protected:
    void accept() override;
    void focusContent() override;
    void errorChanged(bool shown) override;

private:
    std::function<QString(const QString &)> apply_;
    QLineEdit *field_;
};

// Asks to confirm an action: a message, Cancel and an action button, the
// latter focused. Enter confirms unless Cancel has the focus.
class ConfirmPopup : public EntryPopup {
    Q_OBJECT

public:
    // `apply` returns true once done (the popup then closes), false to keep
    // the popup open: it may then show an error, or change the message to
    // ask something else.
    struct Question {
        QString title;
        QString location; // under the title: where it applies
        QString message;
        QString actionText; // the button that confirms
    };

    ConfirmPopup(const Question &question, std::function<bool(ConfirmPopup *)> apply, QWidget *parent);

    void setMessage(const QString &message, const QString &actionText);
    QString message() const;

    QPushButton *actionButton() const { return action_; }

    QPushButton *cancelButton() const { return cancel_; }

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void accept() override;
    void focusContent() override;

private:
    std::function<bool(ConfirmPopup *)> apply_;
    QLabel *message_;
    QPushButton *cancel_;
    QPushButton *action_;
};
