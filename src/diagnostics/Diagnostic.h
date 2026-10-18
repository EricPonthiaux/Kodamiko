#pragma once

#include <QList>
#include <QString>

// A problem found in a text, shown in the editor with a wavy underline and
// its message on hover.
struct Diagnostic {
    enum class Severity {
        Error,
        Warning
    };

    Severity severity = Severity::Error;
    // UTF-8 byte offsets [start, end[ in the text, as Scintilla's positions.
    int start = 0;
    int end = 0;
    QString message;

    bool operator==(const Diagnostic &) const = default;
};

using Diagnostics = QList<Diagnostic>;
