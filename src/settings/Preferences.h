#pragma once

#include "editor/EditorWidget.h"
#include "ui/AppFonts.h"

#include <QFont>
#include <QString>

struct Theme;

// The user's settings (File ▸ Preferences), stored with QSettings. The keys
// are those the menus' checkable commands already use (View ▸ Use System
// Title Bar, Claude ▸ Notifications...), so both stay in step.
struct Preferences {
    // Appearance
    QString theme = QStringLiteral("duet"); // Theme::name
    QString codeFontFamily;                 // empty: the embedded Ubuntu Sans Mono
    int codeFontSize = 11;
    int codeFontWeight = QFont::Normal; // QFont::Light, Normal or Medium
    // Lines of code at this multiple of the font size.
    double codeLineHeight = AppFonts::defaultLineHeight;
    // Programming ligatures in the editor ("->" drawn as an arrow).
    bool ligatures = false;
    // Menus, tabs, panels and dialogs.
    int interfaceFontSize = AppFonts::defaultInterfaceSize;
    // The side trees (files, outline, changes); 0: the code font's size,
    // never below the interface's.
    int treeFontSize = 0;
    bool systemTitleBar = false;

    // Editor
    EditorOptions editor;
    bool formatOnSave = true;
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
    // The editor's MCP server (tools, resources, prompts) in its sessions.
    bool exposeTools = true;
    bool claudeNotifications = true;

    static constexpr int minimumFontSize = 6;
    static constexpr int maximumFontSize = 32;

    static Preferences load();
    void save() const;

    // Font of code (editor, terminal, diffs) for these preferences,
    // without ligatures.
    QFont codeFont() const;
    // The code font, line height and ligatures given to `target`.
    void applyTypography(Theme &target) const;
    // Font of the interface (menus, tabs, panels, dialogs).
    QFont interfaceFont() const;
    // Font of the side trees.
    QFont treeFont() const;

    bool operator==(const Preferences &) const = default;
};
