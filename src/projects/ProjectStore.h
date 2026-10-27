// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ProjectConfig.h"

#include <optional>

// The projects Duet has opened, kept with QSettings under "projects": a map
// from each folder's absolute path to its ProjectConfig. The folders of the
// former "recentFolders" list become projects to set up.
namespace ProjectStore {

// Most recently opened first.
QList<ProjectConfig> all();
std::optional<ProjectConfig> find(const QString &path);
void save(const ProjectConfig &config);
void remove(const QString &path);
// Moves the project of `from` to the folder `to`, which takes its settings
// and its name. False, and nothing changed, when `from` is not a project or
// `to` already is one.
[[nodiscard]] bool relocate(const QString &from, const QString &to);
// Marks `path` as opened now, adding it (not configured) if it is new;
// returns its configuration.
ProjectConfig touch(const QString &path);
// The folder a new window opens: `requested` if not empty, else the last
// folder a window closed on (QSettings "lastFolder"), else the home folder.
QString startFolder(const QString &requested);

} // namespace ProjectStore
