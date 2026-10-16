#pragma once

#include "editor/EditorWidget.h"

#include <QFont>
#include <QString>

// The user's settings (File ▸ Preferences), stored with QSettings. The keys
// are those the menus' checkable commands already use (View ▸ Use System
// Title Bar, Claude ▸ Notifications...), so both stay in step.
struct Preferences {
    // Appearance
    QString theme = QStringLiteral("dark"); // Theme::name
    QString codeFontFamily; // empty: the embedded JetBrains Mono
    int codeFontSize = 11;
    bool systemTitleBar = false;

    // Editor
    EditorOptions editor;
    bool formatOnSave = false;
    // Completion from clangd, jdtls... when installed.
    bool languageServers = true;

    // Files
    bool alwaysSelectOpenedFile = false;

    // Claude
    bool snapshotBeforePrompt = true;
    bool claudeNotifications = true;

    static constexpr int minimumFontSize = 6;
    static constexpr int maximumFontSize = 32;

    static Preferences load();
    void save() const;

    // Font of code (editor, terminal, diffs) for these preferences.
    QFont codeFont() const;

    bool operator==(const Preferences &) const = default;
};
