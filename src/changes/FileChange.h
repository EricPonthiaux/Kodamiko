#pragma once

#include <QString>

#include <stdexcept>

enum class ChangeKind { Added, Modified, Deleted, Renamed };

// Fichier modifié par rapport au dernier commit. Chemins relatifs à la racine
// du projet, séparateurs « / ».
struct FileChange {
    ChangeKind kind;
    QString path;
    QString oldPath; // renseigné seulement pour ChangeKind::Renamed

    bool operator==(const FileChange &) const = default;
};

class GitError : public std::runtime_error {
public:
    explicit GitError(const QString &message)
        : std::runtime_error(message.toStdString())
    {
    }
};
