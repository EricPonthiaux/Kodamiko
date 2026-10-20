// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

enum class LineEnding {
    Lf,
    CrLf,
    Cr
};

// What needs to be known about a file to write it back identically.
struct TextFileFormat {
    bool hasUtf8Bom = false;
    bool isLatin1 = false;                  // file that was not valid UTF-8
    LineEnding lineEnding = LineEnding::Lf; // used for new lines
};

struct DecodedText {
    QString text;
    TextFileFormat format;
};

// Reading and writing text files. The text is never converted: line
// endings stay those of the file, only the line ending of newly typed
// lines depends on `lineEnding`.
namespace TextFile {

DecodedText decode(const QByteArray &bytes);
// `format`, but UTF-8 when ISO-8859-1 cannot hold every character of
// `text`: a "€" typed in such a file changes its encoding rather than
// being written as "?".
TextFileFormat formatFor(const QString &text, TextFileFormat format);
// The bytes of `text` in `formatFor(text, format)`.
QByteArray encode(const QString &text, const TextFileFormat &format);

// nullopt (with `error`) if the file is unreadable or binary.
std::optional<DecodedText> load(const QString &path, QString *error);
bool save(const QString &path, const QString &text, const TextFileFormat &format, QString *error);

} // namespace TextFile
