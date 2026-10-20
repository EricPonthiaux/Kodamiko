#pragma once

#include "ui/OverlayPopup.h"
#include "ui/Theme.h"

class NewProjectForm;

// File ▸ New Project: NewProjectForm over the main window. Created or
// cancelled, it closes; Escape cancels.
class NewProjectPopup : public OverlayPopup {
    Q_OBJECT

public:
    NewProjectPopup(const Theme &theme, QWidget *parent);

    // Shows the form, its name empty.
    void popup();
    void applyTheme(const Theme &theme);

signals:
    // `folder`: the absolute path of the project created, not set up yet.
    void created(const QString &folder);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    NewProjectForm *form_;
};
