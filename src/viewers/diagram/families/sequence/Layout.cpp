// SPDX-License-Identifier: GPL-3.0-only

// The sequence's own layout: message lanes, the room fragments reserve,
// the participants' spacing, and the fragments drawn under the messages.

#include "Lanes.h"
#include "Sequence.h"
#include "viewers/diagram/Board.h"

#include <QHash>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace diagram::sequence {

namespace detail {

QList<const MessageLink *> messages(const Board &board)
{
    QList<const MessageLink *> result;
    for (const auto &l : board.links()) {
        if (const auto *m = dynamic_cast<const MessageLink *>(l.get()))
            result.append(m);
    }
    return result;
}

QList<const MessageLink *> orderedMessages(const Board &board)
{
    QList<const MessageLink *> result = messages(board);
    std::stable_sort(result.begin(), result.end(), [](const MessageLink *a, const MessageLink *b) {
        return a->order.value_or(0) < b->order.value_or(0);
    });
    return result;
}

int maxOrder(const Board &board)
{
    int result = -1;
    for (const MessageLink *m : messages(board)) {
        if (m->order)
            result = std::max(result, *m->order);
    }
    return result;
}

} // namespace detail

using namespace detail;

namespace {

const Font fragmentFont(11);
constexpr qreal messageStep = 44;    // vertical distance between consecutive messages
constexpr qreal fragmentHeader = 24; // room a fragment's header reserves
constexpr qreal branchHeader = 22;   // room a branch separator reserves

// Participants left to right.
QList<ParticipantShape *> participants(const Board &board)
{
    QList<ParticipantShape *> result;
    for (const auto &s : board.shapes()) {
        if (auto *p = dynamic_cast<ParticipantShape *>(s.get()))
            result.append(p);
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const ParticipantShape *a, const ParticipantShape *b) { return a->x < b->x; });
    return result;
}

// Lowest header bottom among participants: where the message lanes start.
qreal laneTop(const Board &board)
{
    qreal top = -std::numeric_limits<qreal>::infinity();
    for (const auto &s : board.shapes()) {
        if (dynamic_cast<const ParticipantShape *>(s.get()))
            top = std::max(top, s->y + s->h);
    }
    return std::isinf(top) ? 0 : top;
}

bool sameBranch(const Fragment &a, const Fragment &b)
{
    return a.branchKeyword == b.branchKeyword && a.branchLabel == b.branchLabel;
}

// How many outer fragments two stacks share.
qsizetype sharedDepth(const QList<Fragment> &a, const QList<Fragment> &b)
{
    qsizetype common = 0;
    while (common < a.size() && common < b.size() && a[common].id == b[common].id)
        ++common;
    return common;
}

// The keyword a branch switch is written with: its own, else Mermaid's
// implicit `and` of a parallel fragment, else `else`.
QString branchKeywordOf(const Fragment &fragment, bool parallelBranches)
{
    if (!fragment.branchKeyword.isEmpty())
        return fragment.branchKeyword;
    const bool isParallel = parallelBranches && fragment.kind == QLatin1String("par");
    return isParallel ? QStringLiteral("and") : QStringLiteral("else");
}

// A fragment statement: its keyword, then its label (encoded) if any.
QString fragmentStatement(const QString &indent,
                          const QString &keyword,
                          const QString &label,
                          const std::function<QString(const QString &)> &encode)
{
    if (label.isEmpty())
        return indent + keyword;
    return indent + keyword + QLatin1Char(' ') + (encode ? encode(label) : label);
}

// The room the fragments reserve above the message of rank `order`: a header
// for each fragment opened on the way, a separator for each branch switched.
qreal structureOffset(const Board &board, int order)
{
    QList<const MessageLink *> list;
    for (const MessageLink *m : orderedMessages(board)) {
        if (m->order.value_or(0) <= order)
            list.append(m);
    }
    QList<Fragment> active;
    qreal offset = 0;
    for (const MessageLink *m : list) {
        const QList<Fragment> &next = m->fragments;
        const qsizetype common = sharedDepth(active, next);
        offset += std::max<qsizetype>(0, next.size() - common) * fragmentHeader;
        for (qsizetype i = 0; i < common; ++i) {
            if (!sameBranch(active[i], next[i]))
                offset += branchHeader;
        }
        active = next;
    }
    return offset;
}

// A fragment as drawn: its extent in ranks and its branches.
struct FragmentSpan {
    Fragment fragment;
    int startOrder = 0;
    int endOrder = 0;

    struct Branch {
        QString signature;
        int order = 0;
        QString keyword;
        QString label;
    };

    QList<Branch> branches;
};

QList<FragmentSpan> fragmentSpans(const Board &board)
{
    QList<FragmentSpan> found;
    QHash<QString, qsizetype> index;
    for (const MessageLink *m : messages(board)) {
        const int order = m->order.value_or(0);
        for (const Fragment &fragment : m->fragments) {
            qsizetype at = index.value(fragment.id, -1);
            if (at < 0) {
                at = found.size();
                index.insert(fragment.id, at);
                found.append({fragment, order, order, {}});
            }
            FragmentSpan &span = found[at];
            span.startOrder = std::min(span.startOrder, order);
            span.endOrder = std::max(span.endOrder, order);
            const QString signature = fragment.branchKeyword + QLatin1Char('\n') + fragment.branchLabel;
            const bool known = std::any_of(span.branches.cbegin(), span.branches.cend(),
                                           [&](const FragmentSpan::Branch &b) { return b.signature == signature; });
            if (!signature.trimmed().isEmpty() && !known)
                span.branches.append({signature, order, fragment.branchKeyword, fragment.branchLabel});
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const FragmentSpan &a, const FragmentSpan &b) {
        if (a.fragment.depth != b.fragment.depth)
            return a.fragment.depth < b.fragment.depth;
        return a.startOrder < b.startOrder;
    });
    return found;
}

// Compacted lanes: the messages keep their relative order, renumbered so
// that the holes left by deletions disappear. Returned in that order.
QList<const MessageLink *> compactLanes(const Board &board)
{
    QList<const MessageLink *> list = messages(board);
    std::stable_sort(list.begin(), list.end(), [](const MessageLink *a, const MessageLink *b) {
        constexpr int last = std::numeric_limits<int>::max();
        return a->order.value_or(last) < b->order.value_or(last);
    });
    for (int i = 0; i < list.size(); ++i)
        list[i]->order = i;
    return list;
}

// The two lifelines a message joins, as indices in the participants row.
struct LifelinePair {
    int from = 0;
    int to = 0;
};

// Widens `gaps` so that a label of `labelWidth` fits between the two
// lifelines (spread over the intervals it crosses); a self-message claims
// the room of its loop on the right.
void claimLabelRoom(QList<qreal> &gaps, const QList<ParticipantShape *> &parts, LifelinePair ends, qreal labelWidth)
{
    if (ends.from == ends.to) {
        const qreal need = selfLoopWidth + labelWidth + 20;
        if (ends.from < gaps.size())
            gaps[ends.from] = std::max(gaps[ends.from], need);
        return;
    }
    const int lo = std::min(ends.from, ends.to);
    const int hi = std::max(ends.from, ends.to);
    qreal centerDistance = 0;
    for (int k = lo; k < hi; ++k)
        centerDistance += parts[k]->w / 2 + gaps[k] + parts[k + 1]->w / 2;
    if (labelWidth <= centerDistance)
        return;
    const qreal extra = (labelWidth - centerDistance) / (hi - lo);
    for (int k = lo; k < hi; ++k)
        gaps[k] += extra;
}

// The gaps between consecutive lifelines: at least minGap everywhere, and
// the room each message's label claims, the shortest spans first.
QList<qreal> lifelineGaps(const Board &board,
                          const QList<ParticipantShape *> &parts,
                          const QList<const MessageLink *> &list)
{
    QHash<QString, int> index;
    for (int i = 0; i < parts.size(); ++i)
        index.insert(parts[i]->id, i);
    const Font labelFont(board.style().linkLabel.fontPx);
    constexpr qreal minGap = 70;
    QList<qreal> gaps(std::max<qsizetype>(parts.size() - 1, 0), minGap);
    const auto spanOf = [&](const MessageLink *l) { return std::abs(index.value(l->from, 0) - index.value(l->to, 0)); };
    QList<const MessageLink *> bySpan = list;
    std::stable_sort(bySpan.begin(), bySpan.end(),
                     [&](const MessageLink *a, const MessageLink *b) { return spanOf(a) < spanOf(b); });
    for (const MessageLink *l : bySpan) {
        if (!index.contains(l->from) || !index.contains(l->to))
            continue;
        const qreal labelWidth =
            l->label.isEmpty() ? 0 : text::width(labelFont, l->label.section(QLatin1Char('\n'), 0, 0)) + 30;
        claimLabelRoom(gaps, parts, {index.value(l->from), index.value(l->to)}, labelWidth);
    }
    return gaps;
}

QString joined(const QString &a, const QString &b)
{
    QStringList parts;
    if (!a.isEmpty())
        parts.append(a);
    if (!b.isEmpty())
        parts.append(b);
    return parts.join(QLatin1Char(' '));
}

} // namespace

// ---------------------------------------------------------------------------
// Fragments

QJsonObject Fragment::toJson() const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("label"), label},
            {QStringLiteral("depth"), depth},
            {QStringLiteral("branchKeyword"), branchKeyword},
            {QStringLiteral("branchLabel"), branchLabel}};
}

Fragment Fragment::fromJson(const QJsonObject &json)
{
    Fragment f;
    f.id = jsonString(json, QStringLiteral("id"));
    f.kind = jsonString(json, QStringLiteral("kind"));
    f.label = jsonString(json, QStringLiteral("label"));
    f.depth = int(jsonNumber(json, QStringLiteral("depth"), 0));
    f.branchKeyword = jsonString(json, QStringLiteral("branchKeyword"));
    f.branchLabel = jsonString(json, QStringLiteral("branchLabel"));
    return f;
}

qreal messageY(const Board &board, int order)
{
    return laneTop(board) + messageStep * (order + 1) + structureOffset(board, order);
}

int nextMessageOrder(const Board &board)
{
    return maxOrder(board) + 1;
}

QList<Fragment> syncFragments(QStringList &out,
                              const QList<Fragment> &active,
                              const QList<Fragment> &next,
                              const FragmentSyntax &syntax)
{
    const QString &indent = syntax.indent;
    const bool parallelBranches = syntax.parallelBranches;
    const std::function<QString(const QString &)> &encode = syntax.encode;
    const qsizetype common = sharedDepth(active, next);
    for (qsizetype i = active.size() - 1; i >= common; --i)
        out.append(indent + QStringLiteral("end"));
    for (qsizetype i = common; i < next.size(); ++i)
        out.append(fragmentStatement(indent, next[i].kind, next[i].label, encode));
    for (qsizetype i = 0; i < common; ++i) {
        const Fragment &after = next[i];
        if (sameBranch(active[i], after))
            continue;
        out.append(fragmentStatement(indent, branchKeywordOf(after, parallelBranches), after.branchLabel, encode));
    }
    return next;
}

// ---------------------------------------------------------------------------
// Layout and underlay

// A dedicated layout: a sequence is not a layered graph, Graphviz would
// scatter it. The participants line up on one row in their current
// left-to-right order, lifelines spaced so that every message label fits
// between its two ends, and the lanes are compacted (the gaps left by
// deletions disappear).
void arrange(const Board &board)
{
    const QList<ParticipantShape *> parts = participants(board);
    if (parts.isEmpty())
        return;
    const QList<qreal> gaps = lifelineGaps(board, parts, compactLanes(board));

    // One row of headers, aligned at the top.
    qreal x = 0;
    for (int i = 0; i < parts.size(); ++i) {
        parts[i]->x = x;
        parts[i]->y = 20;
        x += parts[i]->w + (i < gaps.size() ? gaps[i] : 0);
    }
}

void drawFragments(QPainter &painter, const Board &board)
{
    const QList<ParticipantShape *> parts = participants(board);
    if (parts.isEmpty())
        return;
    const Style &style = board.style();
    const qreal left = parts.front()->cx() - 34;
    const qreal right = parts.back()->cx() + 34;
    for (const FragmentSpan &span : fragmentSpans(board)) {
        const qreal inset = span.fragment.depth * 8;
        const qreal x = left + inset;
        const qreal width = std::max<qreal>(80, right - left - inset * 2);
        const qreal top = messageY(board, span.startOrder) - 44;
        const qreal bottom = messageY(board, span.endOrder) + 22;
        stroke(painter, roundRect(x, top, width, bottom - top, shapeRadiusSm), style.palette.separator, 1.25);

        const QString heading = joined(span.fragment.kind, span.fragment.label);
        if (!heading.isEmpty()) {
            const qreal headingWidth = text::width(fragmentFont, heading) + 16;
            painter.fillRect(QRectF(x + 1, top + 1, headingWidth, 19), style.palette.labelBg);
            text::draw(painter, fragmentFont, heading, QPointF(x + 8, top + 10), style.palette.labelInk, Align::Left,
                       Baseline::Middle);
        }
        for (const FragmentSpan::Branch &branch : span.branches) {
            if (branch.order <= span.startOrder)
                continue;
            const qreal y = messageY(board, branch.order) - 31;
            strokeLine(painter, QPointF(x, y), QPointF(x + width, y), style.palette.separator, 1.25, {5, 4});
            const QString label = joined(branch.keyword, branch.label);
            if (!label.isEmpty()) {
                const qreal labelWidth = text::width(fragmentFont, label) + 12;
                painter.fillRect(QRectF(x + 8, y - 8, labelWidth, 16), style.palette.labelBg);
                text::draw(painter, fragmentFont, label, QPointF(x + 14, y), style.palette.labelInk, Align::Left,
                           Baseline::Middle);
            }
        }
    }
}

} // namespace diagram::sequence
