// SPDX-License-Identifier: GPL-3.0-only

#include "MergeRequest.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace {

using ThreeWayMerge::Chunk;
using Choice = MergeDocument::Choice;

// `lines`, ending with a line ending so that a marker can follow.
QByteArray asBlock(const QByteArrayList &lines)
{
    QByteArray text = lines.join();
    if (!text.isEmpty() && !text.endsWith('\n'))
        text += '\n';
    return text;
}

QByteArray markedConflict(const Chunk &conflict, int number, const Integration::ConflictSides &sides)
{
    const QByteArray label = "conflict " + QByteArray::number(number) + ": ";
    return "<<<<<<< " + label + sides.current.toUtf8() + " (current side)\n" + asBlock(conflict.current) +
           "||||||| common ancestor\n" + asBlock(conflict.base) + "=======\n" + asBlock(conflict.incoming) +
           ">>>>>>> " + label + sides.incoming.toUtf8() + " (incoming side)\n";
}

QJsonObject stringProperty(const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), description}};
}

QJsonObject proposalSchema()
{
    const QJsonObject resolution{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("enum"),
         QJsonArray{QStringLiteral("current"), QStringLiteral("incoming"), QStringLiteral("current_then_incoming"),
                    QStringLiteral("incoming_then_current"), QStringLiteral("lines")}},
        {QStringLiteral("description"),
         QStringLiteral("One side's lines, both sides' in an order, or \"lines\": lines you write.")}};
    const QJsonObject properties{
        {QStringLiteral("conflict"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                     {QStringLiteral("description"), QStringLiteral("The conflict's number, from its markers.")}}},
        {QStringLiteral("resolution"), resolution},
        {QStringLiteral("lines"),
         stringProperty(QStringLiteral("With \"lines\": the lines that replace the conflict, each ending with a "
                                       "line break, without markers."))},
        {QStringLiteral("why"),
         stringProperty(QStringLiteral("One or two sentences: what each side wanted, and how the proposal keeps "
                                       "it."))},
    };
    return {{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), properties},
            {QStringLiteral("required"),
             QJsonArray{QStringLiteral("conflict"), QStringLiteral("resolution"), QStringLiteral("why")}}};
}

// Whether the conflict's lines end with a line ending: the file goes on
// after it, or ends with one.
bool endsWithLineEnding(const Chunk &conflict)
{
    const QByteArrayList &side = conflict.current.isEmpty() ? conflict.incoming : conflict.current;
    return side.isEmpty() || side.last().endsWith('\n');
}

bool usesCrLf(const Chunk &conflict)
{
    const QByteArrayList &side = conflict.current.isEmpty() ? conflict.incoming : conflict.current;
    return !side.isEmpty() && side.first().endsWith("\r\n");
}

// The agent's `text` with the line endings of `conflict`.
QByteArrayList writtenLines(QByteArray text, const Chunk &conflict)
{
    text.replace("\r\n", "\n");
    if (endsWithLineEnding(conflict) && !text.isEmpty() && !text.endsWith('\n'))
        text += '\n';
    if (!endsWithLineEnding(conflict) && text.endsWith('\n'))
        text.chop(1);
    if (usesCrLf(conflict))
        text.replace("\n", "\r\n");
    return ThreeWayMerge::lines(text);
}

std::optional<QByteArrayList> linesOf(const QJsonObject &proposal, const Chunk &conflict)
{
    const QString resolution = proposal.value(QLatin1String("resolution")).toString();
    if (resolution == QLatin1String("current"))
        return conflict.current;
    if (resolution == QLatin1String("incoming"))
        return conflict.incoming;
    if (resolution == QLatin1String("current_then_incoming"))
        return conflict.current + conflict.incoming;
    if (resolution == QLatin1String("incoming_then_current"))
        return conflict.incoming + conflict.current;
    if (resolution == QLatin1String("lines"))
        return writtenLines(proposal.value(QLatin1String("lines")).toString().toUtf8(), conflict);
    return std::nullopt;
}

} // namespace

namespace MergeRequest {

Conflicts conflicts(const MergeDocument &document, const Integration::ConflictSides &sides)
{
    Conflicts result;
    const QList<MergeDocument::Segment> &segments = document.segments();
    for (int i = 0; i < segments.size(); ++i) {
        if (segments[i].choice != Choice::Unresolved) {
            result.text += document.resultLines(i).join();
            continue;
        }
        result.chunks.append(segments[i].chunk);
        result.text += markedConflict(segments[i].chunk, int(result.chunks.size()), sides);
    }
    return result;
}

QString prompt(const Request &request)
{
    return QStringLiteral(
               "%1 stopped on conflicts in %2. The file %3 holds %2 as it would be merged, with its %4 open "
               "conflict(s) between numbered markers: the lines of %5 (the current side), those of the common "
               "ancestor, then those of %6 (the incoming side).\n\n"
               "For each conflict, find out what each side meant: read what you need of the project (the code "
               "that uses these lines, its tests, the rules in CLAUDE.md) and the history with git log if it "
               "helps. Then propose the lines that keep both intents: one side when it already does what the "
               "other wanted, both in an order, or lines you write that combine them. Say why in one or two "
               "sentences, naming what each side wanted. Leave out a conflict that needs a decision only the "
               "user can make.\n\n"
               "Change nothing. Answer with the structured output: a proposal per conflict you resolve.")
        .arg(request.title, request.path, request.copyPath)
        .arg(request.conflictCount)
        .arg(request.sides.current, request.sides.incoming);
}

QJsonObject schema()
{
    const QJsonObject proposals{
        {QStringLiteral("type"), QStringLiteral("array")},
        {QStringLiteral("items"), proposalSchema()},
        {QStringLiteral("description"), QStringLiteral("A proposal per conflict resolved; empty when none can be.")}};
    return {{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QJsonObject{{QStringLiteral("proposals"), proposals}}},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("proposals")}}};
}

std::optional<Proposals> parse(const QString &answer, const QList<ThreeWayMerge::Chunk> &chunks)
{
    const QJsonValue items = QJsonDocument::fromJson(answer.toUtf8()).object().value(QLatin1String("proposals"));
    if (!items.isArray())
        return std::nullopt;
    Proposals proposals;
    for (const QJsonValue &item : items.toArray()) {
        const QJsonObject proposal = item.toObject();
        const int number = proposal.value(QLatin1String("conflict")).toInt();
        if (number < 1 || number > chunks.size())
            continue;
        const Chunk &conflict = chunks[number - 1];
        const std::optional<QByteArrayList> lines = linesOf(proposal, conflict);
        if (!lines)
            continue;
        const QString reason = proposal.value(QLatin1String("why")).toString().trimmed();
        proposals.append({conflict, {*lines, reason}});
    }
    return proposals;
}

} // namespace MergeRequest
