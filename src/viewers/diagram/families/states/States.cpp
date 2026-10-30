// SPDX-License-Identifier: GPL-3.0-only

#include "States.h"

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QCoreApplication>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace diagram::states {

namespace {

const Font font(14, 400, false, true);
constexpr qreal titleFontPx = 17;
const Font titleFont(titleFontPx, 600);
constexpr qreal lineHeight = 22;
constexpr qreal titleLineHeight = 24;
constexpr qreal pad = 18;
// A composite's title band, above its children.
constexpr qreal containerTitleHeight = 44;
constexpr qreal containerMinWidth = 300;
constexpr qreal containerMinHeight = 210;

QString tr(const char *text)
{
    return QCoreApplication::translate("StateDiagram", text);
}

QString defaultContainerText()
{
    return tr("Composite state");
}

QStringList wrapAll(const Font &f, const QStringList &texts, qreal width)
{
    QStringList lines;
    for (const QString &t : texts)
        lines += text::wrap(f, t, width);
    return lines;
}

} // namespace

// ---------------------------------------------------------------------------
// StateElement

StateElement::StateElement(const QString &typeName, qreal width, qreal height)
    : Shape(typeName, width, height)
{
}

void StateElement::load(const QJsonObject &props)
{
    Shape::load(props);
    sourceKey = jsonString(props, QStringLiteral("sourceKey"));
}

QJsonObject StateElement::toJson() const
{
    QJsonObject json = Shape::toJson();
    if (!sourceKey.isEmpty())
        json.insert(QStringLiteral("sourceKey"), sourceKey);
    return json;
}

// ---------------------------------------------------------------------------
// StateShape

StateShape::StateShape()
    : StateElement(QStringLiteral("state"), 180, 64)
{
}

void StateShape::load(const QJsonObject &props)
{
    StateElement::load(props);
    text = jsonString(props, QStringLiteral("text"));
    desc = jsonStrings(props, QStringLiteral("desc"));
    fill = jsonString(props, QStringLiteral("fill"), QStringLiteral("#ffffff"));
    if (fill.isEmpty())
        fill = QStringLiteral("#ffffff");
}

QJsonObject StateShape::toJson() const
{
    QJsonObject json = StateElement::toJson();
    json.insert(QStringLiteral("text"), text);
    json.insert(QStringLiteral("fill"), fill);
    if (!desc.isEmpty())
        json.insert(QStringLiteral("desc"), toJsonArray(desc));
    return json;
}

void StateShape::wrapLines()
{
    lines_ = wrapAll(font, desc, std::max<qreal>(20, w - pad * 2));
    titleLines_ = text::wrap(titleFont, text, std::max<qreal>(20, w - pad * 2));
}

void StateShape::measure(const Board &)
{
    // The width follows the widest of the name and the descriptions, up to a
    // bound past which they wrap; the height follows the lines.
    constexpr qreal maxWidth = 360;
    const qreal widest = std::max(text::widest(titleFont, text::wrap(titleFont, text, maxWidth - pad * 2)),
                                  text::widest(font, wrapAll(font, desc, maxWidth - pad * 2)));
    w = std::ceil(std::max<qreal>(180, std::min(maxWidth, widest + pad * 2)));
    lines_ = wrapAll(font, desc, w - pad * 2);
    titleLines_ = text::wrap(titleFont, text, w - pad * 2);
    const qreal body = lines_.isEmpty() ? 0 : lines_.size() * lineHeight + 20;
    h = headerHeight() + body;
}

qreal StateShape::headerHeight() const
{
    return std::max<qreal>(64, std::max<qsizetype>(1, titleLines_.size()) * titleLineHeight + pad * 2);
}

QColor StateShape::stateFill(const QColor &fallback) const
{
    const QColor own = parseColor(fill);
    return fillColor(!own.isValid() || own == QColor(Qt::white) ? fallback : own);
}

void StateShape::resizeTo(qreal width, qreal height, const Board &board)
{
    StateElement::resizeTo(width, height, board);
    wrapLines();
}

void StateShape::onDoubleClick(Board &board)
{
    // First line: the state's name; the following ones: its descriptions.
    TextEditSpec spec;
    spec.get = [this] { return (QStringList{text} + desc).join(QLatin1Char('\n')); };
    spec.set = [this](const QString &value) {
        QStringList lines = value.split(QLatin1Char('\n'));
        text = lines.isEmpty() ? QString() : lines.takeFirst().trimmed();
        desc.clear();
        for (const QString &line : lines) {
            if (!line.trimmed().isEmpty())
                desc.append(line.trimmed());
        }
    };
    const QColor ink = board.style().state.ink;
    spec.color = readableTextColor(stateFill(board.style().state.fill), ink, ink);
    spec.font = font;
    spec.lineHeight = lineHeight;
    spec.enterCommits = false;
    spec.rect = [this] { return QRectF(x + pad, y + pad, w - pad * 2, h - pad * 2); };
    board.beginTextEdit(this, spec);
}

void StateShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QPainterPath path = roundRect(rect(), 10);
    painter.fillPath(path, stateFill(style.state.fill));
    if (!state.editing) {
        painter.save();
        painter.setClipPath(path, Qt::IntersectClip);
        drawContent(painter, style);
        painter.restore();
    }
    const QColor edge = state.selected ? style.shape.selectedStroke : strokeColor(style.state.stroke);
    stroke(painter, path, edge, state.selected ? 2.5 : 1.2);
}

void StateShape::drawContent(QPainter &painter, const Style &style) const
{
    const qreal band = lines_.isEmpty() ? h : headerHeight();
    const QColor header = stateFill(style.state.header);
    painter.fillRect(QRectF(x, y, w, band), header);
    const QColor ink = readableTextColor(header, style.state.ink, style.state.ink);
    const Align align = lines_.isEmpty() ? Align::Center : Align::Left;
    const qreal left = lines_.isEmpty() ? cx() : x + pad;
    qreal top = y + band / 2 - (titleLines_.size() - 1) * titleLineHeight / 2;
    for (const QString &line : titleLines_) {
        text::draw(painter, titleFont, line, QPointF(left, top), ink, align, Baseline::Middle);
        top += titleLineHeight;
    }
    if (lines_.isEmpty())
        return;
    const QColor body = stateFill(style.state.fill);
    const QColor muted = readableTextColor(body, style.state.mutedInk, style.state.mutedInk);
    const QColor separator = body == style.state.fill ? style.state.separator : mix(body, muted, 0.3);
    strokeLine(painter, QPointF(x, y + band), QPointF(x + w, y + band), separator, 1);
    top = y + band + 10;
    for (const QString &line : lines_) {
        text::draw(painter, font, line, QPointF(x + pad, top), muted, Align::Left, Baseline::Top);
        top += lineHeight;
    }
}

QList<Property> StateShape::properties() const
{
    return {{QStringLiteral("desc"), tr("Description"), Property::Kind::Lines}};
}

QVariant StateShape::propertyValue(const QString &propertyId) const
{
    if (propertyId == QLatin1String("desc"))
        return desc.join(QLatin1Char('\n'));
    return {};
}

void StateShape::setPropertyValue(const QString &propertyId, const QVariant &value)
{
    if (propertyId != QLatin1String("desc"))
        return;
    desc.clear();
    for (const QString &line : value.toString().split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty())
            desc.append(line.trimmed());
    }
    manualWidth.reset();
    manualHeight.reset();
}

// ---------------------------------------------------------------------------
// StateContainerShape

StateContainerShape::StateContainerShape()
    : StateElement(QStringLiteral("stateContainer"), 340, 230)
{
    text = defaultContainerText();
}

void StateContainerShape::load(const QJsonObject &props)
{
    StateElement::load(props);
    text = props.contains(QStringLiteral("text")) ? jsonString(props, QStringLiteral("text")) : defaultContainerText();
}

QJsonObject StateContainerShape::toJson() const
{
    QJsonObject json = StateElement::toJson();
    json.insert(QStringLiteral("text"), text);
    return json;
}

void StateContainerShape::measure(const Board &board)
{
    // An empty composite keeps sidecar's floor, room to drop children in.
    // One that holds children is a Graphviz cluster sized on them: a floor
    // imposed after the layout would push it into its neighbors, or out of
    // its own parent.
    labelWidth_ = text::width(titleFont, text) + pad * 2;
    if (board.childShapes(this).isEmpty())
        minimum_ = QSizeF(std::max(containerMinWidth, labelWidth_ + pad * 2), containerMinHeight);
    else
        minimum_ = QSizeF(labelWidth_ + pad * 2, containerTitleHeight + 40);
    // Its size is the layout's or the user's, never derived from a text: it
    // is kept as chosen, so that the file keeps it too. A size set since the
    // last measure (by the layout) is the one chosen; otherwise the chosen
    // one stands (restored from the file, or resized by hand).
    if (!measured_ || *measured_ != QSizeF(w, h) || !manualWidth || !manualHeight) {
        manualWidth = w;
        manualHeight = h;
    }
    w = std::max(minimum_.width(), *manualWidth);
    h = std::max(minimum_.height(), *manualHeight);
    measured_ = QSizeF(w, h);
}

std::optional<QSizeF> StateContainerShape::resizeMinimum(const Board &) const
{
    return minimum_;
}

void StateContainerShape::resizeTo(qreal width, qreal height, const Board &board)
{
    // The measured size is the one chosen: its children are where the layout
    // put them, or not placed yet while a file is read. A resize by hand
    // still never passes under them.
    if (applyingMeasuredSize) {
        w = std::max(width, resizeMinWidth);
        h = std::max(height, resizeMinHeight);
        return;
    }
    StateElement::resizeTo(width, height, board);
}

bool StateContainerShape::hitTest(QPointF p) const
{
    constexpr qreal tolerance = 9;
    if (!Shape::hitTest(p))
        return false;
    return p.y() <= y + containerTitleHeight || p.x() - x <= tolerance || x + w - p.x() <= tolerance ||
           y + h - p.y() <= tolerance;
}

std::optional<QRectF> StateContainerShape::childBounds(const Board &) const
{
    // Below the title band, off the border (as the free board's frames).
    return QRectF(QPointF(x + 12, y + containerTitleHeight + 8), QPointF(x + w - 12, y + h - 12));
}

qreal StateContainerShape::layoutLabelFontPx() const
{
    // The title really painted: the band Graphviz must reserve on top of the
    // cluster.
    return titleFontPx;
}

std::optional<QSizeF> StateContainerShape::layoutMinimumSize() const
{
    // The floor measure() will take back, whatever Graphviz deduced from the
    // content alone.
    return minimum_;
}

void StateContainerShape::onDoubleClick(Board &board)
{
    TextEditSpec spec;
    spec.get = [this] { return text; };
    spec.set = [this](const QString &value) {
        text = value.trimmed().isEmpty() ? defaultContainerText() : value.trimmed();
    };
    const QColor ink = board.style().state.ink;
    spec.color = readableTextColor(fillColor(board.style().state.containerHeader), ink, ink);
    spec.font = titleFont;
    spec.lineHeight = titleLineHeight;
    spec.rect = [this] { return QRectF(x + pad, y + 10, w - pad * 2, titleLineHeight); };
    board.beginTextEdit(this, spec);
}

void StateContainerShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QPainterPath path = roundRect(rect(), 10);
    painter.fillPath(path, fillColor(style.state.container));
    painter.save();
    painter.setClipPath(path, Qt::IntersectClip);
    painter.fillRect(QRectF(x, y, w, containerTitleHeight), fillColor(style.state.containerHeader));
    painter.restore();
    const QColor edge = state.selected ? style.shape.selectedStroke : strokeColor(style.state.stroke);
    stroke(painter, path, edge, state.selected ? 2.5 : 1.2);
    const QColor ink = readableTextColor(fillColor(style.state.containerHeader), style.state.ink, style.state.ink);
    const QColor separator =
        customFill.isEmpty() ? style.state.separator : mix(fillColor(style.state.container), ink, 0.3);
    strokeLine(painter, QPointF(x, y + containerTitleHeight), QPointF(x + w, y + containerTitleHeight), separator, 1);
    if (state.editing)
        return;
    text::draw(painter, titleFont, text, QPointF(x + pad, y + containerTitleHeight / 2), ink, Align::Left,
               Baseline::Middle);
}

} // namespace diagram::states

// ---------------------------------------------------------------------------
// The family

namespace diagram::families {

const Family &states()
{
    using namespace diagram::states;
    static const Family family = [] {
        Family f;
        f.id = QStringLiteral("states");
        f.title = tr("State diagram");
        f.shapes = {
            {QStringLiteral("state"), [] { return std::unique_ptr<Shape>(new StateShape); }},
            {QStringLiteral("stateContainer"), [] { return std::unique_ptr<Shape>(new StateContainerShape); }},
            {QStringLiteral("initial"),
             [] { return std::unique_ptr<Shape>(new RoundShape(QStringLiteral("initial"), 18)); }},
            {QStringLiteral("final"),
             [] { return std::unique_ptr<Shape>(new RoundShape(QStringLiteral("final"), 22)); }},
            {QStringLiteral("choice"), [] { return std::unique_ptr<Shape>(new ChoiceShape); }},
            {QStringLiteral("fork"), [] { return std::unique_ptr<Shape>(new ForkJoinShape(QStringLiteral("fork"))); }},
            {QStringLiteral("join"), [] { return std::unique_ptr<Shape>(new ForkJoinShape(QStringLiteral("join"))); }},
        };
        // Self-transitions are idiomatic (A --> A : retry), and several events
        // can join the same pair of states.
        f.links = {
            {QStringLiteral("transition"), [] { return std::unique_ptr<Link>(new TransitionLink); }, true, true}};
        f.defaultShape = QStringLiteral("state");
        f.defaultLink = QStringLiteral("transition");
        f.resolveLabels = &diagram::states::resolveLabels;
        f.graphvizClusters = true;
        f.nestedCreation = true;
        f.hasDirection = true;
        // Pseudo-states carry no text: their UML symbol is their meaning.
        const auto preset = [](const char *id, const QString &label, const char *type, const QJsonObject &props) {
            return ElementPreset{QString::fromLatin1(id), label, {}, QString::fromLatin1(type), props};
        };
        f.elements = {
            preset("state-state", tr("State"), "state",
                   {{QStringLiteral("text"), tr("New state")}, {QStringLiteral("desc"), QJsonArray()}}),
            preset("state-container", tr("Composite state"), "stateContainer",
                   {{QStringLiteral("text"), tr("Composite state")}}),
            preset("state-initial", tr("Initial state"), "initial", {}),
            preset("state-final", tr("Final state"), "final", {}),
            preset("state-choice", tr("Choice"), "choice", {{QStringLiteral("text"), tr("Condition?")}}),
            preset("state-fork", tr("Fork"), "fork", {}),
            preset("state-join", tr("Join"), "join", {}),
        };
        return f;
    }();
    return family;
}

} // namespace diagram::families
