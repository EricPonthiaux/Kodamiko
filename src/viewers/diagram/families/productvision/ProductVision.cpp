#include "ProductVision.h"

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QCoreApplication>
#include <QHash>
#include <QPainter>

#include <algorithm>

namespace diagram::productvision {

namespace {

const Font rootTitleFont(11, 700);
const Font rootBodyFont(20, 600);
const Font sectionTitleFont(11, 600);
const Font sectionBodyFont(14);
const Font momentumBodyFont(14, 600);
const Font editFont(13);
constexpr qreal rootLineHeight = 27;
constexpr qreal momentumLineHeight = 20;
constexpr qreal bodyLineHeight = 20;
constexpr qreal padX = 22;
constexpr qreal padY = 20;
// The bullet's room before a section's items.
constexpr qreal bulletIndent = 14;

QString tr(const char *text)
{
    return QCoreApplication::translate("ProductVisionDiagram", text);
}

// A card's rank: the product, the momentum card that stands out under it,
// or an ordinary section.
enum class Tier {
    Root,
    Featured,
    Section
};

Tier tierOf(const QString &role)
{
    if (role == QLatin1String("product"))
        return Tier::Root;
    if (role == QLatin1String("emotions"))
        return Tier::Featured;
    return Tier::Section;
}

const Font &bodyFontOf(Tier tier)
{
    switch (tier) {
    case Tier::Root:
        return rootBodyFont;
    case Tier::Featured:
        return momentumBodyFont;
    case Tier::Section:
        return sectionBodyFont;
    }
    return sectionBodyFont;
}

qreal itemLineHeightOf(Tier tier)
{
    switch (tier) {
    case Tier::Root:
        return rootLineHeight;
    case Tier::Featured:
        return momentumLineHeight;
    case Tier::Section:
        return bodyLineHeight;
    }
    return bodyLineHeight;
}

qreal minimumHeightOf(Tier tier)
{
    switch (tier) {
    case Tier::Root:
        return 138;
    case Tier::Featured:
        return 122;
    case Tier::Section:
        return 120;
    }
    return 120;
}

qreal measuredWidthOf(Tier tier)
{
    switch (tier) {
    case Tier::Root:
        return 430;
    case Tier::Featured:
        return 390;
    case Tier::Section:
        return 280;
    }
    return 280;
}

// The items' native color: white on the product's strong fill, the ink on
// the momentum card, muted elsewhere.
QColor itemColorOf(Tier tier, const Palette &palette)
{
    switch (tier) {
    case Tier::Root:
        return {Qt::white};
    case Tier::Featured:
        return palette.ink;
    case Tier::Section:
        return palette.inkMuted;
    }
    return palette.inkMuted;
}

QColor nativeFillOf(const QString &role, const Tones &tones)
{
    if (tierOf(role) == Tier::Root)
        return tones.strong;
    const QHash<QString, QColor> fills{
        {QStringLiteral("problem"), tones.warm},      {QStringLiteral("emotions"), tones.rose},
        {QStringLiteral("users"), tones.cool},        {QStringLiteral("value"), tones.sage},
        {QStringLiteral("capabilities"), tones.cool}, {QStringLiteral("journeys"), tones.sage},
        {QStringLiteral("unknowns"), tones.rose},
    };
    return fills.value(role, tones.sage);
}

qreal titleLineHeightOf(Tier tier)
{
    return tier == Tier::Root ? 15 : 16;
}

QStringList normalizedItems(const QStringList &values)
{
    QStringList result;
    for (const QString &value : values) {
        const QString item = value.trimmed();
        if (!item.isEmpty())
            result.append(item);
    }
    return result;
}

} // namespace

const QStringList &sections()
{
    static const QStringList list{
        QStringLiteral("emotions"),     QStringLiteral("problem"),  QStringLiteral("users"),   QStringLiteral("value"),
        QStringLiteral("capabilities"), QStringLiteral("journeys"), QStringLiteral("unknowns")};
    return list;
}

QString term(const QString &role)
{
    // sidecar's DIAGRAM_TERMS.en, productVision.*
    static const QHash<QString, const char *> terms{
        {QStringLiteral("product"), "Product"},
        {QStringLiteral("vision"), "Vision"},
        {QStringLiteral("emotions"), "Desired momentum"},
        {QStringLiteral("problem"), "Problem"},
        {QStringLiteral("users"), "Users"},
        {QStringLiteral("value"), "Value proposition"},
        {QStringLiteral("capabilities"), "Main capabilities"},
        {QStringLiteral("journeys"), "Key journeys"},
        {QStringLiteral("unknowns"), "Unknowns to confirm"},
        {QStringLiteral("empty"), "To clarify"},
    };
    const char *text = terms.value(role);
    return text ? tr(text) : role;
}

// ---------------------------------------------------------------------------
// ProductVisionShape

ProductVisionShape::ProductVisionShape()
    : Shape(QStringLiteral("productVision"), 260, 110)
{
}

void ProductVisionShape::load(const QJsonObject &props)
{
    Shape::load(props);
    role = jsonString(props, QStringLiteral("role"), QStringLiteral("problem"));
    if (role.isEmpty())
        role = QStringLiteral("problem");
    if (!props.contains(QStringLiteral("w")))
        w = isRoot() || isFeatured() ? 360 : 260;
    title = jsonString(props, QStringLiteral("title")).trimmed();
    items = normalizedItems(jsonStrings(props, QStringLiteral("items")));
}

QJsonObject ProductVisionShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("role"), role);
    json.insert(QStringLiteral("title"), title);
    json.insert(QStringLiteral("items"), toJsonArray(items));
    return json;
}

QString ProductVisionShape::displayText() const
{
    return title.isEmpty() ? term(role) : title;
}

qreal ProductVisionShape::layoutText()
{
    const Tier tier = tierOf(role);
    const bool root = tier == Tier::Root;
    const qreal inner = w - padX * 2;
    titleLines_ = text::wrap(root ? rootTitleFont : sectionTitleFont, displayText(), inner);
    const QStringList shown = items.isEmpty() ? QStringList{term(QStringLiteral("empty"))} : items;
    itemLines_.clear();
    for (const QString &item : shown)
        itemLines_.append(text::wrap(bodyFontOf(tier), item, inner - (root ? 0 : bulletIndent)));
    itemLineHeight_ = itemLineHeightOf(tier);
    const qreal titleHeight = titleLines_.size() * titleLineHeightOf(tier);
    qreal itemsHeight = 0;
    for (const QStringList &lines : itemLines_)
        itemsHeight += std::max<qsizetype>(1, lines.size()) * itemLineHeight_ + 7;
    return std::max<qreal>(minimumHeightOf(tier), padY * 2 + titleHeight + 10 + itemsHeight);
}

void ProductVisionShape::measure(const Board &)
{
    w = measuredWidthOf(tierOf(role));
    h = layoutText();
}

void ProductVisionShape::resizeTo(qreal width, qreal height, const Board &board)
{
    Shape::resizeTo(width, height, board);
    // A narrower card wraps more: it never gets shorter than its text.
    h = std::max(h, layoutText());
}

void ProductVisionShape::onDoubleClick(Board &board)
{
    // The product edits its name (first line) and its vision; a section, its
    // items. One per line.
    const bool root = isRoot();
    TextEditSpec spec;
    spec.get = [this, root] { return (root ? QStringList{title} + items : items).join(QLatin1Char('\n')); };
    spec.set = [this, root](const QString &value) {
        QStringList lines = normalizedItems(value.split(QLatin1Char('\n')));
        if (root)
            title = lines.isEmpty() ? term(QStringLiteral("product")) : lines.takeFirst();
        items = lines;
    };
    spec.color = root ? QColor(Qt::white) : board.style().palette.ink;
    spec.font = editFont;
    spec.lineHeight = bodyLineHeight;
    spec.rect = [this] { return QRectF(x + padX, y + 12, w - padX * 2, h - 24); };
    spec.enterCommits = false;
    board.beginTextEdit(this, spec);
}

void ProductVisionShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QPainterPath path = roundRect(x, y, w, h, shapeRadiusSm);
    shadowFill(painter, path, fillColor(nativeFillOf(role, style.tones)), style);
    stroke(painter, path, strokeColor(state.selected ? style.shape.selectedStroke : style.palette.border),
           state.selected ? 2 : shapeBorderWidth);
    if (state.editing)
        return;
    const qreal itemsTop = drawTitle(painter, style) + 10;
    drawItems(painter, style, itemsTop);
}

qreal ProductVisionShape::drawTitle(QPainter &painter, const Style &style) const
{
    const Tier tier = tierOf(role);
    const bool root = tier == Tier::Root;
    // The product's title is a translucent white over its strong fill.
    const QColor color = textColor(root ? QColor(255, 255, 255, qRound(0.78 * 255)) : style.palette.ink);
    const Font &titleFont = root ? rootTitleFont : sectionTitleFont;
    qreal ty = y + padY;
    for (const QString &line : titleLines_) {
        text::draw(painter, titleFont, line.toUpper(), QPointF(x + padX, ty), color, Align::Left, Baseline::Top);
        ty += titleLineHeightOf(tier);
    }
    return ty;
}

void ProductVisionShape::drawItems(QPainter &painter, const Style &style, qreal top) const
{
    const Tier tier = tierOf(role);
    // The product's vision reads as prose; a section's items are bulleted.
    const bool bulleted = tier != Tier::Root;
    const Font &body = bodyFontOf(tier);
    const QColor color = textColor(itemColorOf(tier, style.palette));
    const qreal textX = x + padX + (bulleted ? bulletIndent : 0);
    qreal ty = top;
    for (const QStringList &lines : itemLines_) {
        if (bulleted)
            painter.fillPath(ellipse(QPointF(x + padX + 3, ty + 7), 2.2, 2.2), color);
        for (const QString &line : lines) {
            text::draw(painter, body, line, QPointF(textX, ty), color, Align::Left, Baseline::Top);
            ty += itemLineHeight_;
        }
        ty += 7;
    }
}

// ---------------------------------------------------------------------------
// ProductVisionLink

ProductVisionLink::ProductVisionLink()
    : Link(QStringLiteral("productVisionLink"))
{
}

bool ProductVisionLink::isCentral(const Board &board) const
{
    const auto *a = dynamic_cast<const ProductVisionShape *>(board.shape(from));
    const auto *b = dynamic_cast<const ProductVisionShape *>(board.shape(to));
    return a && b && a->role == QLatin1String("product") && b->role == QLatin1String("emotions");
}

LinkStyle ProductVisionLink::drawStyle(const Board &board, bool selected) const
{
    if (!isCentral(board))
        return Link::drawStyle(board, selected);
    LinkStyle style = Link::drawStyle(board, selected, board.style().link.width + .9);
    if (!parseColor(customColor).isValid())
        style.color = board.style().shape.selectedStroke;
    return style;
}

void ProductVisionLink::draw(QPainter &painter, const Board &board, bool selected) const
{
    // The arrows stay in the background of the cards, the central one less
    // so; a color chosen by the user is shown as is.
    painter.save();
    painter.setOpacity(painter.opacity() * opacity(board, selected));
    Link::draw(painter, board, selected);
    painter.restore();
}

qreal ProductVisionLink::opacity(const Board &board, bool selected) const
{
    if (!customColor.isEmpty())
        return 1;
    if (selected)
        return .9;
    return isCentral(board) ? .72 : .38;
}

} // namespace diagram::productvision

// ---------------------------------------------------------------------------
// The family

namespace diagram::families {

const Family &productVision()
{
    using namespace diagram::productvision;
    static const Family family = [] {
        Family f;
        f.id = QStringLiteral("productvision");
        f.title = tr("Product vision");
        f.shapes = {{QStringLiteral("productVision"), [] { return std::unique_ptr<Shape>(new ProductVisionShape); }}};
        f.links = {{QStringLiteral("productVisionLink"), [] { return std::unique_ptr<Link>(new ProductVisionLink); }}};
        // A fixed skeleton: the cards and arrows come from the source alone.
        // sidecar names default types, so that a double-click in empty space
        // makes a card the source cannot hold (and that cannot be deleted);
        // without defaults, nothing is created or connected by hand.
        f.allowDeletion = false;
        f.graphvizSplines = QStringLiteral("line");
        f.alwaysArrange = true;
        return f;
    }();
    return family;
}

} // namespace diagram::families
