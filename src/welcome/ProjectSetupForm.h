#pragma once

#include "projects/ProjectConfig.h"
#include "projects/ProjectDetector.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QVBoxLayout;
class SettingsCard;
class Toolchains;
class ToggleSwitch;

// The setup of a project's languages, one card each: whether it is used,
// its runtime (JDK, Node, Python, C++ standard), its language server and
// the server's version. Each version says whether it is downloaded
// already, on the machine, or still to download.
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

    // A setup for `folder` from what its files say.
    static ProjectConfig detectedProject(const QString &folder, const Toolchains &toolchains,
                                         QList<DetectedLanguage> *detected = nullptr);

signals:
    void changed();

private:
    struct Card {
        QString family;
        QLabel *evidence;
        ToggleSwitch *enabled;
        QComboBox *runtime;
        QComboBox *server;
        QComboBox *serverVersion;
        QList<QWidget *> rows; // hidden when turned off
    };
    void build(Card &card);
    void fill(Card &card, const LanguageSetup &setup, const QString &evidence);
    void fillRuntimes(Card &card, const LanguageSetup &setup);
    void fillServerVersions(Card &card, const QString &server, const QString &selected);
    void updateRows(Card &card);
    LanguageSetup setupOf(const Card &card) const;

    Toolchains *toolchains_;
    ProjectConfig project_;
    QList<Card> cards_;
    QVBoxLayout *layout_;
};
