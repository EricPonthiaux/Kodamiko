#pragma once

#include "diagnostics/Problem.h"

#include <QByteArray>
#include <QSet>
#include <QString>
#include <QStringList>

// Reads the problems in the output of a build as it arrives, terminal
// escapes included: GCC and Clang, MSVC, javac (alone, through Maven or
// Gradle), Kotlin, TypeScript, ESLint, Rust, Go, CMake and Python's
// tracebacks. A problem is kept only when its file exists, relative paths
// read from the command's folder, then from the project's.
class BuildOutputParser {
public:
    BuildOutputParser(QString workingDirectory, QString projectFolder);

    // Raw bytes of the program's output.
    void feed(QByteArrayView data);
    // The output ended: reads its last line and what was pending.
    void finish();

    const Problems &problems() const { return problems_; }

    // `line` without its terminal escapes (colors, titles...) and, after a
    // carriage return, only what was written over it.
    static QString cleanLine(const QString &line);

private:
    void parseLine(const QString &line);
    // Problems that span lines: Rust's header waits for its location,
    // CMake's location for its message, Python's frames for the exception.
    bool continuePending(const QString &line);
    void flushPending();
    void add(const QString &path, int line, int column, Diagnostic::Severity severity, const QString &message);
    QString resolve(QString path) const;

    QString workingDirectory_;
    QString projectFolder_;
    QByteArray buffer_; // bytes after the last complete line
    Problems problems_;
    QSet<QString> seen_;

    enum class Pending {
        None,
        Rust,
        CMake,
        Python
    };
    Pending pending_ = Pending::None;
    Diagnostic::Severity pendingSeverity_ = Diagnostic::Severity::Error;
    QString pendingMessage_;
    QString pendingPath_;
    int pendingLine_ = 0;
    // The last frame of a Python traceback in the project.
    QString pythonPath_;
    int pythonLine_ = 0;
    // The file ESLint's stylish output lists problems under.
    QString eslintFile_;
};
