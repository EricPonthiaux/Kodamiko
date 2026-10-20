// SPDX-License-Identifier: GPL-3.0-only

#include "GraphvizFormat.h"

#include "Board.h"
#include "Graphviz.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace diagram::graphviz {

namespace {

// A point "x,y" of Graphviz's frame (origin at the bottom), in the board's.
std::optional<QPointF> parsePoint(const QString &x, const QString &y, qreal height)
{
    bool okX = false;
    bool okY = false;
    const QPointF point(x.toDouble(&okX), height - y.toDouble(&okY));
    if (!okX || !okY)
        return std::nullopt;
    return point;
}

// Adds one token of an edge's `pos` to `spline`: an end ("e,x,y"), a start
// ("s,x,y") or a control point ("x,y"). False if it is malformed.
bool addPosToken(Spline &spline, const QString &token, qreal height)
{
    const QStringList parts = token.split(QLatin1Char(','));
    const bool isEnd = parts.size() == 3 && parts[0] == QLatin1String("e");
    const bool isStart = parts.size() == 3 && parts[0] == QLatin1String("s");
    if (isEnd || isStart) {
        const std::optional<QPointF> point = parsePoint(parts[1], parts[2], height);
        (isEnd ? spline.end : spline.start) = point;
        return point.has_value();
    }
    if (parts.size() != 2)
        return true;
    const std::optional<QPointF> point = parsePoint(parts[0], parts[1], height);
    if (!point)
        return false;
    spline.points.push_back(*point);
    return true;
}

} // namespace

QString number(qreal value, int decimals)
{
    return QString::number(value, 'f', decimals);
}

QString endpoint(const QString &id, const GraphvizEndpoint *end)
{
    QString result = QLatin1Char('"') + dotEscape(end && !end->id.isEmpty() ? end->id : id) + QLatin1Char('"');
    if (end && !end->port.isEmpty())
        result += QStringLiteral(":\"") + dotEscape(end->port) + QLatin1Char('"');
    static const QSet<QString> compasses{
        QStringLiteral("n"),  QStringLiteral("ne"), QStringLiteral("e"),  QStringLiteral("se"), QStringLiteral("s"),
        QStringLiteral("sw"), QStringLiteral("w"),  QStringLiteral("nw"), QStringLiteral("c"),  QStringLiteral("_")};
    if (end && compasses.contains(end->compass))
        result += QLatin1Char(':') + end->compass;
    return result;
}

QStringList linkAttributes(const Link &link, const Board &board)
{
    QStringList attributes = link.graphvizAttributes(board);
    if (link.customWidth) {
        const LinkStyle style = link.drawStyle(board, false);
        qreal nativeSize = board.style().link.arrow / 10;
        for (const QString &attribute : attributes) {
            if (attribute.startsWith(QStringLiteral("arrowsize=")))
                nativeSize = attribute.mid(10).toDouble();
        }
        attributes.append(QStringLiteral("penwidth=") + number(*link.customWidth));
        attributes.append(QStringLiteral("arrowsize=") + number(nativeSize * style.markerScale));
    }
    return attributes;
}

std::vector<qreal> numbers(const QString &text)
{
    std::vector<qreal> result;
    for (const QString &part : text.split(QLatin1Char(','))) {
        bool ok = false;
        const qreal value = part.toDouble(&ok);
        if (!ok)
            return {};
        result.push_back(value);
    }
    return result;
}

std::optional<Spline> parseEdgePos(const QString &pos, qreal height)
{
    if (pos.isEmpty())
        return std::nullopt;
    Spline spline;
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    for (const QString &token : pos.trimmed().split(spaces, Qt::SkipEmptyParts)) {
        if (!addPosToken(spline, token, height))
            return std::nullopt;
    }
    if (spline.points.size() < 4 || (spline.points.size() - 1) % 3 != 0)
        return std::nullopt;
    return spline;
}

QSizeF layoutSize(const Shape &shape)
{
    const std::optional<QSizeF> declared = shape.graphvizLayoutSize();
    return {std::max<qreal>(1, declared ? declared->width() : shape.w),
            std::max<qreal>(1, declared ? declared->height() : shape.h)};
}

std::vector<qreal> boundingBox(const QJsonObject &object)
{
    return numbers(object.value(QStringLiteral("bb")).toString());
}

} // namespace diagram::graphviz
