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
    static const QStringList list{QStringLiteral("emotions"),     QStringLiteral("problem"),  QStringLiteral("users"),
                                  QStringLiteral("value"),        QStringLiteral("capabilities"),
                                  QStringLiteral("journeys"),     QStringLiteral("unknowns")};
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
    const bool root = isRoot(), featured = isFeatured();
    const qreal inner = w - padX * 2;
    titleLines_ = text::wrap(root ? rootTitleFont : sectionTitleFont, displayText(), inner);
    const Font &body = root ? rootBodyFont : featured ? momentumBodyFont : sectionBodyFont;
    const QStringList shown = items.isEmpty() ? QStringList{term(QStringLiteral("empty"))} : items;
    itemLines_.clear();
    for (const QString &item : shown)
        itemLines_.append(text::wrap(body, item, inner - (root ? 0 : bulletIndent)));
    itemLineHeight_ = root ? rootLineHeight : featured ? momentumLineHeight : bodyLineHeight;
    const qreal titleHeight = titleLines_.size() * (root ? 15 : 16);
    qreal itemsHeight = 0;
    for (const QStringList &lines : itemLines_)
        itemsHeight += std::max<qsizetype>(1, lines.size()) * itemLineHeight_ + 7;
    return std::max<qreal>(root ? 138 : featured ? 122 : 120, padY * 2 + titleHeight + 10 + itemsHeight);
}

void ProductVisionShape::measure(const Board &)
{
    w = isRoot() ? 430 : isFeatured() ? 390 : 280;
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
    const bool root = isRoot(), featured = isFeatured();
    const Style &style = board.style();
    const Tones &tones = style.tones;
    const QHash<QString, QColor> fills{
        {QStringLiteral("problem"), tones.warm},  {QStringLiteral("emotions"), tones.rose},
        {QStringLiteral("users"), tones.cool},    {QStringLiteral("value"), tones.sage},
        {QStringLiteral("capabilities"), tones.cool}, {QStringLiteral("journeys"), tones.sage},
        {QStringLiteral("unknowns"), tones.rose},
    };
    const QColor nativeFill = root ? tones.strong : fills.value(role, tones.sage);
    const QPainterPath path = roundRect(x, y, w, h, ShapeRadiusSm);
    shadowFill(painter, path, fillColor(nativeFill), style);
    stroke(painter, path, strokeColor(state.selected ? style.shape.selectedStroke : style.palette.border),
           state.selected ? 2 : ShapeBorderWidth);
    if (state.editing)
        return;

    const QColor ink = textColor(root ? QColor(Qt::white) : style.palette.ink);
    const QColor muted = textColor(root ? QColor(255, 255, 255, qRound(0.78 * 255)) : style.palette.inkMuted);
    const QColor featuredInk = textColor(style.palette.ink);
    qreal ty = y + padY;
    const Font &titleFont = root ? rootTitleFont : sectionTitleFont;
    for (const QString &line : titleLines_) {
        text::draw(painter, titleFont, line.toUpper(), QPointF(x + padX, ty), root ? muted : ink, Align::Left,
                   Baseline::Top);
        ty += root ? 15 : 16;
    }
    ty += 10;
    const Font &body = root ? rootBodyFont : featured ? momentumBodyFont : sectionBodyFont;
    const QColor bodyColor = root ? ink : featured ? featuredInk : muted;
    for (const QStringList &lines : itemLines_) {
        qreal lineY = ty;
        if (!root)
            painter.fillPath(ellipse(QPointF(x + padX + 3, ty + 7), 2.2, 2.2), bodyColor);
        for (const QString &line : lines) {
            text::draw(painter, body, line, QPointF(x + padX + (root ? 0 : bulletIndent), lineY), bodyColor,
                       Align::Left, Baseline::Top);
            lineY += itemLineHeight_;
        }
        ty = lineY + 7;
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
    painter.setOpacity(painter.opacity()
                       * (!customColor.isEmpty() ? 1 : selected ? .9 : isCentral(board) ? .72 : .38));
    Link::draw(painter, board, selected);
    painter.restore();
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
