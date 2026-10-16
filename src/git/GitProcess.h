#pragma once

#include <QByteArray>
#include <QHash>
#include <QStringList>

namespace Git {

// Runs "git <arguments>" in `workDir`, waits for it to finish and returns its
// standard output. `input` is written to its standard input; `environment`
// is added to the editor's. git never prompts for anything: no message
// editor, no credential prompt, and paths passed after "--" are taken
// literally.
// Throws GitError (with git's error output) on failure.
QByteArray run(const QString &workDir, const QStringList &arguments, const QByteArray &input = {},
               const QHash<QString, QString> &environment = {});

// Splits a "-z" output (fields terminated by a null byte).
QStringList splitNul(const QByteArray &output);

} // namespace Git
