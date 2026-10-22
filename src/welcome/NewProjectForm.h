// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "recipes/Recipe.h"
#include "ui/Theme.h"

#include <QWidget>

class PopupHeader;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// Creating a project, for the welcome window and File ▸ New Project: its
// name, the folder it goes in, the recipe it is made with (those of this
// system, see RecipeLibrary) and whether it gets a git repository. Create
// writes the folder, adds it to the projects (ProjectStore, not set up yet,
// the recipe attached to its language) and emits created: the host then
// sets its languages up, detected from the recipe's files.
class NewProjectForm : public QWidget {
    Q_OBJECT

public:
    explicit NewProjectForm(QWidget *parent = nullptr);

    // Empties the name and gives it the focus; the location, the recipe
    // and the git choice stay those of the last project created.
    void reset();
    void applyTheme(const Theme &theme);

    // The fields and the Create button. For tests.
    QLineEdit *nameEdit() const { return name_; }

    QLineEdit *locationEdit() const { return location_; }

    QListWidget *recipeList() const { return recipeList_; }

    QCheckBox *gitCheck() const { return git_; }

    QPushButton *createButton() const { return create_; }

signals:
    // `folder`: the absolute path of the project created.
    void created(const QString &folder);
    void cancelled();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *createBody();
    QWidget *createFooter();
    void browse();
    // The project's folder: the name in the location.
    QString targetFolder() const;
    // Why Create cannot run, for the footer; empty when it can.
    QString problem() const;
    void updateState();
    void create();
    void initRepository(const QString &folder);
    void trustForClaude(const QString &folder);
    // The recipe of the selected item, if any.
    const Recipe *selectedRecipe() const;

    PopupHeader *header_;
    QLineEdit *name_;
    QLineEdit *location_;
    QListWidget *recipeList_;
    QList<Recipe> recipes_; // the items' recipes, in the same order
    QLabel *description_;
    QCheckBox *git_;
    QLabel *summary_ = nullptr;
    QPushButton *create_ = nullptr;
};
