#include "Journey.h"

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QCoreApplication>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace diagram::journey {

namespace {

const Font bodyFont(13);
const Font titleFont(14, 600);
const Font headerFont(11);
constexpr qreal innerPad = 16;
constexpr qreal maxWidth = 240;
constexpr qreal headerLineH = 15;
constexpr qreal titleLineH = 20;
constexpr qreal actorLineH = 17;
// The scores' fills when the board is not colorful.
const char *const legacyScore[] = {"#eef2f7", "#e9eef5", "#e5edf5", "#e0ebf4", "#d9e8f4"};

QString tr(const char *text)
{
    return QCoreApplication::translate("JourneyDiagram", text);
}

int clampScore(qreal value)
{
    const int score = int(value);
    return score ? std::clamp(score, 1, 5) : 3;
}

QStringList splitActors(const QString &value)
{
    QStringList result;
    for (const QString &actor : value.split(QLatin1Char(','))) {
        if (!actor.trimmed().isEmpty())
            result.append(actor.trimmed());
    }
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// JourneyTaskShape

JourneyTaskShape::JourneyTaskShape()
    : Shape(QStringLiteral("journey"), 190, 96)
    , text(tr("Step"))
    , section(tr("Journey"))
{
}

void JourneyTaskShape::load(const QJsonObject &props)
{
    Shape::load(props);
    text = jsonString(props, QStringLiteral("text"), tr("Step"));
    section = jsonString(props, QStringLiteral("section"), tr("Journey"));
    const QJsonValue value = props.value(QStringLiteral("score"));
    score = clampScore(value.isString() ? value.toString().toDouble() : value.toDouble());
    actors = jsonStrings(props, QStringLiteral("actors"));
}

QJsonObject JourneyTaskShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("text"), text);
    json.insert(QStringLiteral("section"), section);
    json.insert(QStringLiteral("score"), score);
    json.insert(QStringLiteral("actors"), toJsonArray(actors));
    return json;
}

JourneyTaskShape::TextLayout JourneyTaskShape::textLayout(qreal width) const
{
    const QString dots = QString(score, QChar(0x25CF)) + QString(5 - score, QChar(0x25CB));
    TextLayout layout;
    layout.header = text::wrap(headerFont, section + QStringLiteral(" · ") + dots, width);
    layout.title = text::wrap(titleFont, text, width);
    if (!actors.isEmpty())
        layout.actors = text::wrap(bodyFont, actors.join(QStringLiteral(", ")), width);
    return layout;
}

void JourneyTaskShape::measure(const Board &)
{
    const TextLayout probe = textLayout(maxWidth - innerPad * 2);
    const qreal widest = std::max({text::widest(headerFont, probe.header), text::widest(titleFont, probe.title),
                                   text::widest(bodyFont, probe.actors)});
    w = std::max<qreal>(170, std::min(maxWidth, widest + innerPad * 2));
    const TextLayout layout = textLayout(w - innerPad * 2);
    const qreal content = layout.header.size() * headerLineH + 6 + layout.title.size() * titleLineH +
                          (layout.actors.isEmpty() ? 0 : 7 + layout.actors.size() * actorLineH);
    h = std::max<qreal>(96, innerPad * 2 + content);
}

void JourneyTaskShape::onDoubleClick(Board &board)
{
    // Three lines: the task, its score, its actors.
    TextEditSpec spec;
    spec.get = [this] {
        return QStringLiteral("%1\n%2\n%3").arg(text).arg(score).arg(actors.join(QStringLiteral(", ")));
    };
    spec.set = [this](const QString &value) {
        const QStringList lines = value.split(QLatin1Char('\n'));
        const QString task = lines.value(0).trimmed();
        text = task.isEmpty() ? tr("Step") : task;
        score = clampScore(lines.value(1, QStringLiteral("3")).trimmed().toDouble());
        actors = splitActors(lines.value(2));
    };
    spec.color = board.style().palette.ink;
    spec.font = bodyFont;
    spec.lineHeight = 18;
    spec.rect = [this] { return QRectF(x + innerPad, y + innerPad, w - innerPad * 2, h - innerPad * 2); };
    spec.enterCommits = false;
    board.beginTextEdit(this, spec);
}

void JourneyTaskShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    // The mood of the step: rose for a pain point, cool for a delight.
    const QColor scoreTones[] = {style.tones.rose, style.tones.warm, style.tones.warm, style.tones.sage,
                                 style.tones.cool};
    const QColor nativeFill = style.tones.colorful ? scoreTones[score - 1] : QColor(legacyScore[score - 1]);
    const QColor ink = textColor(style.palette.ink);
    const QColor muted = textColor(style.palette.inkMuted);
    const QPainterPath outline = roundRect(x, y, w, h, ShapeRadiusSm);
    shadowFill(painter, outline, fillColor(nativeFill), style);
    stroke(painter, outline, strokeColor(state.selected ? style.shape.selectedStroke : style.palette.border),
           state.selected ? 2 : ShapeBorderWidth);
    if (state.editing)
        return;
    const TextLayout layout = textLayout(w - innerPad * 2);
    qreal ty = y + innerPad;
    for (const QString &line : layout.header) {
        text::draw(painter, headerFont, line, QPointF(cx(), ty), muted, Align::Center, Baseline::Top);
        ty += headerLineH;
    }
    ty += 6;
    for (const QString &line : layout.title) {
        text::draw(painter, titleFont, line, QPointF(cx(), ty), ink, Align::Center, Baseline::Top);
        ty += titleLineH;
    }
    if (!layout.actors.isEmpty()) {
        ty += 7;
        for (const QString &line : layout.actors) {
            text::draw(painter, bodyFont, line, QPointF(cx(), ty), muted, Align::Center, Baseline::Top);
            ty += actorLineH;
        }
    }
}

QList<Property> JourneyTaskShape::properties() const
{
    return {
        {QStringLiteral("section"), tr("Section"), Property::Kind::Text, {}},
        {QStringLiteral("score"),
         tr("Score"),
         Property::Kind::Choice,
         {{QStringLiteral("1"), tr("1 · Very unhappy")},
          {QStringLiteral("2"), tr("2 · Unhappy")},
          {QStringLiteral("3"), tr("3 · Neutral")},
          {QStringLiteral("4"), tr("4 · Happy")},
          {QStringLiteral("5"), tr("5 · Very happy")}}},
        {QStringLiteral("actors"), tr("Actors (comma-separated)"), Property::Kind::Text, {}},
    };
}

QVariant JourneyTaskShape::propertyValue(const QString &id) const
{
    if (id == QLatin1String("section"))
        return section;
    if (id == QLatin1String("score"))
        return QString::number(score);
    if (id == QLatin1String("actors"))
        return actors.join(QStringLiteral(", "));
    return {};
}

void JourneyTaskShape::setPropertyValue(const QString &id, const QVariant &value)
{
    if (id == QLatin1String("section"))
        section = value.toString().trimmed();
    else if (id == QLatin1String("score"))
        score = clampScore(value.toString().toDouble());
    else if (id == QLatin1String("actors"))
        actors = splitActors(value.toString());
}

// ---------------------------------------------------------------------------
// JourneyLink

JourneyLink::JourneyLink()
    : Link(QStringLiteral("journeyLink"))
{
}

} // namespace diagram::journey

// ---------------------------------------------------------------------------
// The family

namespace diagram::families {

const Family &journey()
{
    using namespace diagram::journey;
    static const Family family = [] {
        Family f;
        f.id = QStringLiteral("journey");
        f.title = tr("User journey");
        f.shapes = {{QStringLiteral("journey"), [] { return std::unique_ptr<Shape>(new JourneyTaskShape); }}};
        f.links = {{QStringLiteral("journeyLink"), [] { return std::unique_ptr<Link>(new JourneyLink); }}};
        f.defaultShape = QStringLiteral("journey");
        f.defaultLink = QStringLiteral("journeyLink");
        return f;
    }();
    return family;
}

} // namespace diagram::families
