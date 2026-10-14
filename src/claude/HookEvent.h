#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

// Notification envoyée par un hook de Claude Code (voir ClaudeLauncher).
struct HookEvent {
    enum class Kind {
        FilesEdited,      // PostToolUse sur Edit/Write/MultiEdit/NotebookEdit
        ResponseFinished, // Stop : fin de la réponse
    };

    Kind kind;
    QString sessionId;
    QStringList filePaths; // FilesEdited, chemins absolus

    // Décode le JSON transmis par Claude Code sur l'entrée du hook ;
    // nullopt pour un JSON invalide ou un événement qui ne nous concerne pas.
    static std::optional<HookEvent> fromJson(const QByteArray &json);
};
