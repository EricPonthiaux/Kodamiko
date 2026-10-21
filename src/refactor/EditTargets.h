// SPDX-License-Identifier: GPL-3.0-only

#pragma once

class EditorTabs;
class LanguageServers;

// What a refactoring changes besides the files on disk: the open editors,
// and the language servers' view of the documents. None is owned.
struct EditTargets {
    EditorTabs *tabs = nullptr;
    LanguageServers *servers = nullptr;
};
