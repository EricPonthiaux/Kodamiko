// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QFileInfoList>

#include <functional>

// Files of the working folder as the project sees them, for Go to File,
// Find in Files and the symbol index.
namespace ProjectFiles {

struct Options {
    // Hidden files and folders (".github"…); ".git" is always left out.
    bool hidden = false;
    // Filter applied before the ignore rules, which are slower to consult.
    std::function<bool(const QFileInfo &)> accept;
    int maxFiles = 100000;
};

// Files under `folder`, sorted by path within each folder. Left out: files
// and folders ignored by git, node_modules and symbolic links to folders.
// Usable off the UI thread.
QFileInfoList list(const QString &folder, const Options &options = {});

} // namespace ProjectFiles
