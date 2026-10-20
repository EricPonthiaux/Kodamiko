// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projects/ProjectConfig.h"
#include "projects/ProjectDetector.h"
#include "ui/Theme.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class Toolchains;

// The setup of a project's languages, in two columns as Preferences: at the
// left, the languages (those the project uses first, the others marked
// Off) and Detect Again; at the right, the selected one's settings: whether
// it is used, its runtime (JDK, Node, Python, C++ standard), its language
// server and the server's version. Each list shows the values alone; the
// line under it says whether the one chosen is downloaded already, on the
// machine (and where), or still to download.
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

    QString currentLanguage() const;
    // Shows `family`'s settings.
    void selectLanguage(const QString &family);

    // A setup for `folder` from what its files say.
    static ProjectConfig detectedProject(const QString &folder,
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
        QComboBox *runtime{};
        QLabel *runtimeState{};
        QComboBox *server{};
        QComboBox *serverVersion{};
        QLabel *serverState{};
        QWidget *settings{};   // hidden when turned off
        QLabel *offHint{};     // shown instead
        QWidget *versionRow{}; // hidden without a server
    };

    QWidget *createSidebar();
    QWidget *createPage(Page &page);
    void connectPage(const Page &page);
    Page &pageOf(const QString &family);
    void fill(Page &page, const LanguageSetup &setup, const QString &evidence);
    void fillServerVersions(const Page &page, const QString &server, const QString &selected);
    void updatePage(const Page &page);
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
};
