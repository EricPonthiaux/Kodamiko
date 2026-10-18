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

const Font font(13);
constexpr qreal titleFontPx = 14;
const Font titleFont(titleFontPx, 600);
constexpr qreal lineHeight = 20;
constexpr qreal pad = 14;
// A composite's title band, above its children.
constexpr qreal containerTitleHeight = 38;
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

QPainterPath circle(QPointF center, qreal radius)
{
    return ellipse(center, radius, radius);
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
    : StateElement(QStringLiteral("state"), 150, 60)
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
    w = std::ceil(std::max<qreal>(150, std::min(maxWidth, widest + pad * 2)));
    lines_ = wrapAll(font, desc, w - pad * 2);
    titleLines_ = text::wrap(titleFont, text, w - pad * 2);
    const qsizetype titleCount = std::max<qsizetype>(1, titleLines_.size());
    const qreal body = lines_.isEmpty() ? 0 : lines_.size() * lineHeight + pad;
    h = std::max<qreal>(48, titleCount * lineHeight + pad * 2 + body);
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
    spec.color = board.style().palette.ink;
    spec.font = font;
    spec.lineHeight = lineHeight;
    spec.enterCommits = false;
    spec.rect = [this] { return QRectF(x + pad, y + pad, w - pad * 2, h - pad * 2); };
    board.beginTextEdit(this, spec);
}

void StateShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QColor own = parseColor(fill);
    const QColor nativeFill = !own.isValid() || own == QColor(Qt::white) ? style.tones.sage : own;
    const QPainterPath path = roundRect(x, y, w, h, shapeRadiusSm);
    shadowFill(painter, path, fillColor(nativeFill), style);
    stroke(painter, path, strokeColor(state.selected ? style.shape.selectedStroke : style.palette.border),
           state.selected ? 2 : shapeBorderWidth);
    if (state.editing)
        return;
    painter.save();
    painter.setClipPath(path, Qt::IntersectClip);
    const QColor ink = textColor(style.palette.ink);
    // Without a description the name floats dead center; otherwise it heads
    // the box.
    qreal ty = !lines_.isEmpty() ? y + pad + lineHeight / 2 : cy() - ((titleLines_.size() - 1) * lineHeight) / 2;
    for (const QString &line : titleLines_) {
        text::draw(painter, titleFont, line, QPointF(cx(), ty), ink, Align::Center, Baseline::Middle);
        ty += lineHeight;
    }
    ty -= lineHeight / 2;
    if (!lines_.isEmpty()) {
        ty += pad / 2;
        strokeLine(painter, QPointF(x, ty - pad / 4), QPointF(x + w, ty - pad / 4), style.palette.separator, 1);
        ty += pad / 2;
        const QColor muted = textColor(style.palette.inkMuted);
        for (const QString &line : lines_) {
            text::draw(painter, font, line, QPointF(x + pad, ty), muted, Align::Left, Baseline::Top);
            ty += lineHeight;
        }
    }
    painter.restore();
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
    spec.color = board.style().palette.ink;
    spec.font = font;
    spec.lineHeight = lineHeight;
    spec.rect = [this] { return QRectF(x + pad, y + 9, w - pad * 2, 22); };
    board.beginTextEdit(this, spec);
}

void StateContainerShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QPainterPath path = roundRect(x, y, w, h, shapeRadiusSm);
    painter.save();
    painter.setOpacity(painter.opacity() * (customFill.isEmpty() ? 0.72 : 0.55));
    shadowFill(painter, path, fillColor(style.tones.group.isValid() ? style.tones.group : style.tones.sage), style);
    painter.restore();
    stroke(painter, path, strokeColor(state.selected ? style.shape.selectedStroke : style.palette.border),
           state.selected ? 2 : shapeBorderWidth);
    strokeLine(painter, QPointF(x, y + containerTitleHeight), QPointF(x + w, y + containerTitleHeight),
               style.palette.separator, 1);
    if (state.editing)
        return;
    text::draw(painter, titleFont, text, QPointF(x + pad, y + containerTitleHeight / 2), textColor(style.palette.ink),
               Align::Left, Baseline::Middle);
}

// ---------------------------------------------------------------------------
// RoundShape

RoundShape::RoundShape(const QString &typeName, qreal size)
    : StateElement(typeName, size, size)
{
}

bool RoundShape::hitTest(QPointF p) const
{
    const qreal dx = p.x() - cx();
    const qreal dy = p.y() - cy();
    return dx * dx + dy * dy <= (w / 2) * (w / 2);
}

QPointF RoundShape::borderPoint(QPointF toward) const
{
    const qreal dx = toward.x() - cx();
    const qreal dy = toward.y() - cy();
    if (!dx && !dy)
        return {cx(), cy()};
    const qreal t = (w / 2) / std::hypot(dx, dy);
    return {cx() + dx * t, cy() + dy * t};
}

QString RoundShape::displayText() const
{
    return type == QLatin1String("initial") ? tr("Initial state") : tr("Final state");
}

void RoundShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QPointF center(cx(), cy());
    const QPainterPath outer = circle(center, w / 2);
    if (type == QLatin1String("initial")) {
        shadowFill(painter, outer, fillColor(state.selected ? style.shape.selectedStroke : style.palette.solid), style);
        if (!customFill.isEmpty())
            stroke(painter, outer, strokeColor(style.palette.solid), shapeBorderWidth);
        return;
    }
    // The final state: a ring around a bullet.
    const QColor color = fillColor(state.selected ? style.shape.selectedStroke : style.palette.solid);
    shadowFill(painter, outer, style.background, style);
    stroke(painter, outer, strokeColor(color), 1.5);
    painter.fillPath(circle(center, w / 2 - 4), color);
}

// ---------------------------------------------------------------------------
// ChoiceShape

ChoiceShape::ChoiceShape()
    : StateElement(QStringLiteral("choice"), 36, 36)
{
}

void ChoiceShape::load(const QJsonObject &props)
{
    StateElement::load(props);
    text = jsonString(props, QStringLiteral("text"));
}

QJsonObject ChoiceShape::toJson() const
{
    QJsonObject json = StateElement::toJson();
    json.insert(QStringLiteral("text"), text);
    return json;
}

void ChoiceShape::measure(const Board &)
{
    if (text.trimmed().isEmpty()) {
        w = h = 36;
        lines_.clear();
        return;
    }
    const qreal widest = text::widest(font, text::wrap(font, text, 140));
    // A diamond narrows toward its vertices: the extra room keeps every line
    // inside its central readable area.
    w = std::ceil(std::max<qreal>(96, std::min<qreal>(210, widest + 52)));
    lines_ = text::wrap(font, text, w - 52);
    h = std::ceil(std::max<qreal>(72, lines_.size() * lineHeight + 42));
}

void ChoiceShape::resizeTo(qreal width, qreal height, const Board &board)
{
    StateElement::resizeTo(width, height, board);
    lines_ = text.trimmed().isEmpty() ? QStringList() : text::wrap(font, text, std::max<qreal>(20, w - 52));
}

void ChoiceShape::onDoubleClick(Board &board)
{
    TextEditSpec spec;
    spec.get = [this] { return text; };
    spec.set = [this](const QString &value) { text = value.trimmed(); };
    spec.color = board.style().palette.ink;
    spec.font = font;
    spec.lineHeight = lineHeight;
    spec.align = Align::Center;
    spec.rect = [this] { return QRectF(x + w * 0.22, y + h * 0.24, w * 0.56, h * 0.52); };
    board.beginTextEdit(this, spec);
}

QPointF ChoiceShape::borderPoint(QPointF toward) const
{
    const qreal dx = toward.x() - cx();
    const qreal dy = toward.y() - cy();
    if (!dx && !dy)
        return {cx(), cy()};
    const qreal t = 1 / (std::abs(dx) / (w / 2) + std::abs(dy) / (h / 2));
    return {cx() + dx * t, cy() + dy * t};
}

QStringList ChoiceShape::graphvizNodeAttributes(const Board &, const QString &, qreal width, qreal height) const
{
    // Graphviz routes against the visible diamond, not the measurement box
    // around it: otherwise transitions stop short of the outline.
    return {QStringLiteral("shape=diamond"),
            QStringLiteral("fixedsize=true"),
            QStringLiteral("margin=0"),
            QStringLiteral("label=\"\""),
            QStringLiteral("width=") + QString::number(std::max<qreal>(1, width) / 72, 'f', 3),
            QStringLiteral("height=") + QString::number(std::max<qreal>(1, height) / 72, 'f', 3)};
}

QPainterPath ChoiceShape::outline() const
{
    return polygon({{cx(), y}, {x + w, cy()}, {cx(), y + h}, {x, cy()}});
}

void ChoiceShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QPainterPath path = outline();
    shadowFill(painter, path, fillColor(style.tones.warm), style);
    stroke(painter, path, strokeColor(state.selected ? style.shape.selectedStroke : style.palette.border),
           state.selected ? 2 : shapeBorderWidth);
    if (state.editing || text.isEmpty())
        return;
    painter.save();
    painter.setClipPath(path, Qt::IntersectClip);
    const QColor ink = textColor(style.palette.ink);
    const QStringList lines = lines_.isEmpty() ? QStringList{text} : lines_;
    qreal ty = cy() - ((lines.size() - 1) * lineHeight) / 2;
    for (const QString &line : lines) {
        text::draw(painter, font, line, QPointF(cx(), ty), ink, Align::Center, Baseline::Middle);
        ty += lineHeight;
    }
    painter.restore();
}

// ---------------------------------------------------------------------------
// ForkJoinShape

ForkJoinShape::ForkJoinShape(const QString &typeName)
    : StateElement(typeName, 100, 12)
{
}

QString ForkJoinShape::displayText() const
{
    return type == QLatin1String("fork") ? tr("Fork") : tr("Join");
}

void ForkJoinShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QPainterPath path = roundRect(x, y, w, h, shapeRadiusSm);
    shadowFill(painter, path, fillColor(state.selected ? style.shape.selectedStroke : style.palette.solid), style);
    if (!customFill.isEmpty())
        stroke(painter, path, strokeColor(style.palette.solid), shapeBorderWidth);
}

QList<Property> ForkJoinShape::properties() const
{
    return {{QStringLiteral("kind"),
             tr("Kind"),
             Property::Kind::Choice,
             {{QStringLiteral("fork"), tr("Fork")}, {QStringLiteral("join"), tr("Join")}}}};
}

QVariant ForkJoinShape::propertyValue(const QString &propertyId) const
{
    return propertyId == QLatin1String("kind") ? QVariant(type) : QVariant();
}

void ForkJoinShape::setPropertyValue(const QString &propertyId, const QVariant &value)
{
    // Same bar, other meaning: the type follows the choice.
    const bool isBarKind = value == QLatin1String("fork") || value == QLatin1String("join");
    if (propertyId == QLatin1String("kind") && isBarKind)
        type = value.toString();
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
