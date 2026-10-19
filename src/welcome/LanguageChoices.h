#pragma once

#include "projects/ProjectConfig.h"

#include <QString>
#include <QVariant>

class QComboBox;
class Toolchains;

// The lists of a language's settings (ProjectSetupForm): each item shows a
// value alone ("JDK 21", "clangd", "21.1.0") and holds, in stateRole, the
// line shown under the list once chosen: whether it is downloaded already,
// on this machine (and where), or still to download.
namespace LanguageChoices {

constexpr int stateRole = Qt::UserRole + 1;

// The runtime chosen in a list of fillRuntimes.
struct Runtime {
    QString version;
    QString path; // a Python's executable; empty for the others
};

// The runtimes of `setup`'s language: C++ standards, the Pythons of the
// machine (those in `projectPath` named after it), JDKs or Node.js
// versions. Selects `setup`'s.
void fillRuntimes(QComboBox *combo,
                  const LanguageSetup &setup,
                  const Toolchains &toolchains,
                  const QString &projectPath);
Runtime selectedRuntime(const QComboBox *combo);

// `family`'s language servers, then None. Selects `selected`.
void fillServers(QComboBox *combo, const QString &family, const QString &selected);
// `server`'s versions: the system's, then the others, newest first; none
// without a server. Selects `selected`.
void fillServerVersions(QComboBox *combo, const QString &server, const QString &selected, const Toolchains &toolchains);

} // namespace LanguageChoices
