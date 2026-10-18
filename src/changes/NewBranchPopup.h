#pragma once

#include "git/RepositoryStatus.h"
#include "ui/OverlayPopup.h"
#include "ui/Theme.h"

class PopupHeader;
class QLabel;
class QLineEdit;
class QPushButton;
class QCheckBox;

// Git ▸ New Branch, a small popup over the window: the branch's name,
// checked as it is typed, and whether to switch to it. The branch starts at
// HEAD.
class NewBranchPopup : public OverlayPopup {
    Q_OBJECT

public:
    NewBranchPopup(const Theme &theme, QWidget *parent);

    // Shows the popup for `repository`; `existing` are its local branches.
    void popup(const RepositoryInfo &repository, const QStringList &existing);
    // popup() without showing, for tests.
    void load(const RepositoryInfo &repository, const QStringList &existing);
    void applyTheme(const Theme &theme);

    QLineEdit *nameField() const { return name_; }

    QCheckBox *switchToggle() const { return switchTo_; }

    QPushButton *createButton() const { return create_; }

    QLabel *message() const { return message_; }

    // Emits createRequested if the name is valid.
    void create();

signals:
    void createRequested(const QString &name, bool switchTo);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void onNameEdited(const QString &text);
    void validate();

    Theme theme_;
    QStringList existing_;
    PopupHeader *header_;
    QLineEdit *name_;
    QLabel *message_;
    QCheckBox *switchTo_;
    QPushButton *create_;
};
