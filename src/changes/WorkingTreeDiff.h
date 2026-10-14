#pragma once

#include "FileChange.h"

#include <QList>

// Fichier modifié dans le répertoire de travail, avec son diff unifié.
struct WorkingTreeChange {
    FileChange change;
    QString patch;
};

// Changements en cours d'un projet : différence entre le dernier commit du
// dépôt git de l'utilisateur et les fichiers sur disque, fichiers non suivis
// compris. Le dépôt est seulement lu. Lève GitError en cas d'échec.
class WorkingTreeDiff {
public:
    // `folder` peut être un sous-dossier du dépôt : seuls ses fichiers sont
    // comparés. Chemins renvoyés relatifs à `folder`, séparateurs « / ».
    // Renvoie false si `folder` n'appartient à aucun dépôt git.
    static bool compute(const QString &folder, QList<WorkingTreeChange> *changes);
};
