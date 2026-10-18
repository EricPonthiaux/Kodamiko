#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// A diagram drawn by Claude on request, outside the editor's session: a
// one-off `claude --print` run with Duet's plugin (the diagrams skill),
// allowed to read the project and to write the diagram's file only. Its
// stream-json output tells what Claude is doing, for the loading screen.
namespace DiagramRequest {

// What Claude is asked: draw a `kindTitle` in `relativePath`, showing
// `description`.
QString prompt(const QString &kindTitle, const QString &relativePath, const QString &description);

// The arguments that follow --print. `hookCommand`, when given, checks the
// diagram after each write (PostToolUse hook, see DiagramCheck);
// `pluginPath` provides the diagrams skill.
QStringList arguments(const QString &prompt, const QString &relativePath, const QString &pluginPath,
                      const QString &hookCommand);

// One line of the stream-json output.
struct Event {
    enum class Kind {
        None,
        Steps,
        Finished
    };
    Kind kind = Kind::None;
    QStringList steps;   // Steps: what Claude does, "Reading src/a.cpp"
    bool failed = false; // Finished
    QString text;        // Finished: Claude's answer, or the error
};
// File paths in steps are shown relative to `workDir`.
Event parseLine(const QByteArray &line, const QString &workDir);

} // namespace DiagramRequest
