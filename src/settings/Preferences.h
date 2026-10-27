// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "editor/EditorWidget.h"
#include "terminal/OptionKey.h"
#include "ui/AppFonts.h"

#include <QFont>
#include <QString>
#include <QStringList>

struct Theme;

// The user's settings (File ▸ Preferences), stored with QSettings. The keys
// are those the menus' checkable commands already use (View ▸ Use System
// Title Bar, Claude ▸ Notifications...), so both stay in step.
struct Preferences {
    // Appearance
    QString theme = QStringLiteral("duet-violet"); // Theme::name
    QString codeFontFamily;                      // empty: the embedded Droid Sans Mono
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
    bool animations = true;

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

    // Agent
    // The agent of the projects that have not chosen one (see
    // AgentProviders::key()).
    QString defaultAgentProvider = QStringLiteral("claude-code");
    bool snapshotBeforePrompt = true;
    bool shareEditorContext = true;
    // The errors an edit brought, told to the agent right after it.
    bool reportEditErrors = true;
    // The editor's MCP server (tools, resources, prompts) in its sessions.
    bool exposeTools = true;
    bool claudeNotifications = true;

    // Terminal
    // Put first in the PATH of every program Duet launches in a terminal,
    // in this order.
    QStringList terminalPathFolders;
    // macOS only: Option composes characters, or acts as Meta.
    OptionKey terminalOptionKey = OptionKey::Compose;

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
