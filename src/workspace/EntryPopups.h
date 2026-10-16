#pragma once

#include <QFrame>

#include <functional>

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

// Small popup for an operation on a file or folder, in the manner of
// IntelliJ's "New File": a bold title with the location in gray next to it,
// then the content. Enter applies, Escape or a click outside cancels.
// Errors appear in red at the bottom. It deletes itself when closed.
class EntryPopup : public QFrame {
    Q_OBJECT

public:
    // Shows the popup centered in the parent's window.
    void popup();

    QString error() const;
    void showError(const QString &message);

protected:
    EntryPopup(const QString &title, const QString &location, QWidget *parent);

    void keyPressEvent(QKeyEvent *event) override;
    virtual void accept() = 0;
    virtual void focusContent() = 0;

    QVBoxLayout *content() const { return content_; }
    // Called when the error appears or disappears.
    virtual void errorChanged(bool shown) { Q_UNUSED(shown); }

private:
    QVBoxLayout *content_;
    QLabel *error_;
};

// Asks for a name: a single field, outlined in red while an error is
// shown; the error disappears as soon as the name is edited.
class NamePopup : public EntryPopup {
    Q_OBJECT

public:
    // `apply` receives the typed name (trimmed) and returns an error
    // message, or an empty string once done: the popup then closes.
    NamePopup(const QString &title, const QString &location, std::function<QString(const QString &)> apply,
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
    ConfirmPopup(const QString &title, const QString &location, const QString &message, const QString &actionText,
                 std::function<bool(ConfirmPopup *)> apply, QWidget *parent);

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
