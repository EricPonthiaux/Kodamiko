#pragma once

#include "Diagnostic.h"

#include <QList>
#include <QString>

// A problem of a file, wherever it was found: reported by a language
// server, or read in the output of a build.
struct Problem {
    QString path; // absolute
    int line = 0; // from 0
    // Characters from the start of the line, from 0 (UTF-16 code units, as
    // LSP's); -1 when the tool did not say.
    int column = -1;
    // Where it ends, -1 when it is not known: the word at `column` (the
    // line when there is no column).
    int endLine = -1;
    int endColumn = -1;
    Diagnostic::Severity severity = Diagnostic::Severity::Error;
    QString message;
    // "clangd", "tsc"... for a server; the run configuration for a build.
    QString source;

    bool operator==(const Problem &) const = default;
};

using Problems = QList<Problem>;
