#pragma once

#include "terminal/ProcessSpec.h"

#include <QByteArray>
#include <QString>

#include <optional>

// Prépare le lancement de la CLI `claude` dans un terminal de l'éditeur.
//
// Les hooks qui signalent l'activité de Claude sont passés par `--settings` : la
// configuration Claude de l'utilisateur n'est jamais modifiée, et ces hooks
// n'existent que pour les sessions lancées depuis l'éditeur.
namespace ClaudeLauncher {

// Contenu JSON du fichier --settings : hooks PostToolUse (outils d'édition)
// et Stop, qui lancent tous deux `hookCommand`.
QByteArray hooksSettings(const QString &hookCommand);

// Programme `claude` trouvé, ou chaîne vide.
QString findClaudeExecutable();

// Processus à lancer, ou nullopt (avec `error` renseigné) si `claude` ou
// `enlumine-hook` sont introuvables.
std::optional<ProcessSpec> claudeProcess(const QString &workingDirectory, const QString &hookServerName,
                                         QString *error);

} // namespace ClaudeLauncher
