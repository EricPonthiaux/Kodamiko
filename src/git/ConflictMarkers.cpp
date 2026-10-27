// SPDX-License-Identifier: GPL-3.0-only

#include "ConflictMarkers.h"

#include <algorithm>

namespace ConflictMarkers {
namespace {

enum class Marker {
    Start,
    Base,
    Separator,
    End,
};

// Where the parts of the conflict being read are: those after `stage` are
// still to come.
enum class Stage {
    Current,
    Base,
    Incoming,
};

// A line that holds a marker.
struct MarkerLine {
    Marker marker;
    int line = 0;
    Section bytes;
    QString label;
};

// git's default; a conflict-marker-size attribute can change it.
constexpr qsizetype markerSize = 7;

QByteArrayView withoutLineEnding(QByteArrayView line)
{
    if (line.endsWith('\n'))
        line.chop(1);
    if (line.endsWith('\r'))
        line.chop(1);
    return line;
}

// The marker `line` starts with: seven times the same character, then the
// end of the line or, for those that take one, a space and a label.
std::optional<Marker> markerOf(QByteArrayView line)
{
    if (line.size() < markerSize)
        return std::nullopt;
    const char first = line.front();
    if (!std::all_of(line.begin(), line.begin() + markerSize, [first](char c) { return c == first; }))
        return std::nullopt;
    const bool endsAfterMarker = line.size() == markerSize;
    const bool takesLabel = endsAfterMarker || line[markerSize] == ' ';
    switch (first) {
    case '<':
        return takesLabel ? std::optional(Marker::Start) : std::nullopt;
    case '|':
        return takesLabel ? std::optional(Marker::Base) : std::nullopt;
    case '=':
        return endsAfterMarker ? std::optional(Marker::Separator) : std::nullopt;
    case '>':
        return takesLabel ? std::optional(Marker::End) : std::nullopt;
    default:
        return std::nullopt;
    }
}

QString labelOf(QByteArrayView line)
{
    return QString::fromUtf8(line.sliced(markerSize)).trimmed();
}

// A conflict whose end marker is still to come.
struct OpenConflict {
    Conflict conflict;
    Stage stage = Stage::Current;
};

// Reads the markers one after the other, and keeps the conflicts they
// close.
class Reader {
public:
    void read(const MarkerLine &mark)
    {
        if (mark.marker == Marker::Start) {
            open_ = OpenConflict{.conflict = {.span = {mark.bytes.start, 0},
                                              .current = {mark.bytes.end, 0},
                                              .startLine = mark.line,
                                              .currentLabel = mark.label}};
            return;
        }
        if (!open_)
            return;
        if (advance(*open_, mark)) {
            conflicts_.append(open_->conflict);
            open_.reset();
        }
    }

    QList<Conflict> conflicts() const { return conflicts_; }

private:
    // Takes `mark` into `open`; returns true when it closes it.
    static bool advance(OpenConflict &open, const MarkerLine &mark)
    {
        Conflict &conflict = open.conflict;
        switch (mark.marker) {
        case Marker::Start:
            return false;
        case Marker::Base:
            if (open.stage == Stage::Current) {
                conflict.current.end = mark.bytes.start;
                conflict.baseLine = mark.line;
                open.stage = Stage::Base;
            }
            return false;
        case Marker::Separator:
            if (open.stage == Stage::Current)
                conflict.current.end = mark.bytes.start;
            if (open.stage != Stage::Incoming) {
                conflict.separatorLine = mark.line;
                conflict.incoming.start = mark.bytes.end;
                open.stage = Stage::Incoming;
            }
            return false;
        case Marker::End:
            if (open.stage != Stage::Incoming)
                return false;
            conflict.incoming.end = mark.bytes.start;
            conflict.span.end = mark.bytes.end;
            conflict.endLine = mark.line;
            conflict.incomingLabel = mark.label;
            return true;
        }
        return false;
    }

    QList<Conflict> conflicts_;
    std::optional<OpenConflict> open_;
};

} // namespace

QList<Conflict> find(QByteArrayView text)
{
    // Most files have none: no need to go through their lines.
    if (!text.contains(QByteArrayView("<<<<<<<")))
        return {};
    Reader reader;
    int line = 0;
    for (qsizetype start = 0; start < text.size(); ++line) {
        const qsizetype newline = text.indexOf('\n', start);
        const qsizetype end = newline < 0 ? text.size() : newline + 1;
        const QByteArrayView content = withoutLineEnding(text.sliced(start, end - start));
        if (const std::optional<Marker> marker = markerOf(content)) {
            const QString label = *marker == Marker::Separator ? QString() : labelOf(content);
            reader.read({.marker = *marker, .line = line, .bytes = {int(start), int(end)}, .label = label});
        }
        start = end;
    }
    return reader.conflicts();
}

Replacement resolve(QByteArrayView text, const Conflict &conflict, Resolution resolution)
{
    const auto bytesOf = [text](const Section &section) {
        return text.sliced(section.start, section.end - section.start).toByteArray();
    };
    switch (resolution) {
    case Resolution::Current:
        return {conflict.span, bytesOf(conflict.current)};
    case Resolution::Incoming:
        return {conflict.span, bytesOf(conflict.incoming)};
    case Resolution::Both:
        return {conflict.span, bytesOf(conflict.current) + bytesOf(conflict.incoming)};
    }
    return {conflict.span, {}};
}

} // namespace ConflictMarkers
