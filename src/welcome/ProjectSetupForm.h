// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projects/ProjectConfig.h"
#include "projects/ProjectDetector.h"
#include "recipes/Recipe.h"
#include "ui/Theme.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class Toolchains;

// The setup of a project's languages: at the left, each language's tool
// availability (those the project uses first) and Detect Again; at the
// right, the selected one's settings: whether
// it is used, its recipe (one of the language's, see RecipeLibrary: its
// environment, skills, prompts and run configurations, without its files),
// its runtime (JDK, Node, Python, C++ standard), its language server and the
// server's version. Each list shows the values alone; the line under it
// says whether the one chosen is downloaded already, on the machine (and
// where), or still to download. The recipe card describes its tools and
// commands; the summary also accounts for missing tools Kodamiko cannot install.
class ProjectSetupForm : public QWidget {
    Q_OBJECT

public:
    explicit ProjectSetupForm(Toolchains *toolchains, QWidget *parent = nullptr);

    // Shows `project`'s setup; `detected` says why each language was found.
    // The languages it lacks are shown turned off.
    void setProject(const ProjectConfig &project, const QList<DetectedLanguage> &detected);
    // The setup as shown, configured.
    ProjectConfig project() const;
    // Detects the project's languages again and shows what was found.
    void detectAgain();
    void applyTheme(const Theme &theme);

    // The languages, in the order shown, and the one shown at the right.
    // For tests.
    QListWidget *languageList() const { return list_; }

    // The recipe choice of `family`'s page. For tests.
    QComboBox *recipeChoice(const QString &family);
    QString currentLanguage() const;
    // Shows `family`'s settings.
    void selectLanguage(const QString &family);

    // A setup for `saved.path` from what its files say, keeping `saved`'s
    // project options and the recipes of its enabled languages (their
    // language kept even when no file shows it yet).
    static ProjectConfig detectedProject(const ProjectConfig &saved,
                                         const Toolchains &toolchains,
                                         QList<DetectedLanguage> *detected = nullptr);

signals:
    void changed();

private:
    // The settings of one language, at the right.
    struct Page {
        QString family;
        QLabel *evidence{};
        QCheckBox *enabled{};
        QComboBox *recipe{};
        QLabel *recipeState{};
        QLabel *recipeTags{};
        QComboBox *runtime{};
        QLabel *runtimeState{};
        QComboBox *server{};
        QComboBox *serverVersion{};
        QLabel *serverState{};
        QWidget *settings{};   // hidden when turned off
        QWidget *offHint{};    // shown instead
        QWidget *versionRow{}; // hidden without a server
        QWidget *availability{};
        QLabel *toolTitle{};
        QLabel *toolDetail{};
        QLabel *toolBadge{};
        QString status;
    };

    QWidget *createSidebar();
    // The language's recipes for this system, `selected` among them (kept,
    // marked missing, when it is not installed).
    void fillRecipes(const Page &page, const QString &selected);
    QWidget *createPage(Page &page);
    QWidget *createLanguageHeading(Page &page);
    QWidget *createRecipeCard(Page &page);
    QWidget *createSettings(Page &page);
    QWidget *createOffHint(const Page &page);
    QWidget *createAvailability(Page &page);
    void connectPage(const Page &page);
    Page &pageOf(const QString &family);
    void fill(Page &page, const LanguageSetup &setup, const QString &evidence);
    void fillServerVersions(const Page &page, const QString &server, const QString &selected);
    void updatePage(Page &page);
    void updateRecipe(const Page &page);
    // The version of the page's server `recipe` sets, if it sets one:
    // chosen, and not to change.
    void showRecipeServer(const Page &page, const Recipe &recipe) const;
    void updateAvailability(Page &page);
    // The languages used first; `keep` stays selected.
    void fillList(const QString &keep);
    void updateListItem(const Page &page);
    LanguageSetup setupOf(const Page &page) const;

    Toolchains *toolchains_;
    ProjectConfig project_;
    QList<Page> pages_;
    QListWidget *list_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QPushButton *detect_ = nullptr;
    QLabel *activeCount_ = nullptr;
    QList<Recipe> recipes_; // those of this system, for the project
};
