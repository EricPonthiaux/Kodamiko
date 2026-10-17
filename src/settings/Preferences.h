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
    // Menus, tabs, panels and dialogs; 0: the system's size.
    int interfaceFontSize = 0;
    // The side trees (files, outline, changes); 0: the code font's size,
    // never below the interface's.
    int treeFontSize = 0;
    bool systemTitleBar = false;

    // Editor
    EditorOptions editor;
    bool formatOnSave = false;
    // Completion from clangd, jdtls... when installed.
    bool languageServers = true;
    // Full Line completion: the rest of the line from a code model served
    // on this machine (see FullLineModel).
    bool fullLineCompletion = false;
    QString fullLineServer = QStringLiteral("ollama"); // or "llama.cpp"
    QString fullLineEndpoint = QStringLiteral("http://localhost:11434");
    QString fullLineModel = QStringLiteral("qwen2.5-coder:1.5b");

    // Files
    bool alwaysSelectOpenedFile = false;

    // Claude
    bool snapshotBeforePrompt = true;
    bool shareEditorContext = true;
    bool claudeNotifications = true;

    static constexpr int minimumFontSize = 6;
    static constexpr int maximumFontSize = 32;

    static Preferences load();
    void save() const;

    // Font of code (editor, terminal, diffs) for these preferences.
    QFont codeFont() const;
    // Font of the interface (menus, tabs, panels, dialogs).
    QFont interfaceFont() const;
    // Font of the side trees.
    QFont treeFont() const;

    bool operator==(const Preferences &) const = default;
};
