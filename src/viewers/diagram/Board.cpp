#include "Board.h"

#include "Family.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace diagram {

namespace {

constexpr size_t historyLimit = 200;

qreal clamp(qreal value, qreal low, qreal high)
{
    return std::max(low, std::min(value, high));
}

QJsonArray sortedById(QJsonArray items)
{
    std::vector<QJsonObject> list;
    for (const QJsonValue &item : items)
        list.push_back(item.toObject());
    std::sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("id")).toString() < b.value(QStringLiteral("id")).toString();
    });
    QJsonArray result;
    for (const QJsonObject &item : list)
        result.append(item);
    return result;
}

} // namespace

Board::Board(QObject *parent)
    : QObject(parent)
    , style_(Style::printed())
{
}

Board::~Board() = default;

void Board::setFamily(const Family *family)
{
    clear();
    family_ = family;
    clearHistory();
}

void Board::setStyle(const Style &style)
{
    style_ = style;
    emit changed();
}

Shape *Board::shape(const QString &id) const
{
    return shapeIndex_.value(id);
}

Link *Board::link(const QString &id) const
{
    return linkIndex_.value(id);
}

Shape *Board::shapeByReference(const QString &reference) const
{
    if (reference.isEmpty())
        return nullptr;
    if (Shape *found = shape(reference))
        return found;
    for (const auto &s : shapes_) {
        if (s->key == reference)
            return s.get();
    }
    return nullptr;
}

QList<Shape *> Board::childShapes(const Shape *parent) const
{
    QList<Shape *> result;
    if (!parent)
        return result;
    for (const auto &s : shapes_) {
        if (s.get() == parent || s->parentBoundary.isEmpty())
            continue;
        if (s->parentBoundary == parent->id || (!parent->key.isEmpty() && s->parentBoundary == parent->key))
            result.append(s.get());
    }
    return result;
}

QList<Shape *> Board::descendantShapes(const Shape *parent) const
{
    QList<Shape *> result;
    QList<Shape *> queue = childShapes(parent);
    QSet<QString> seen;
    while (!queue.isEmpty()) {
        Shape *s = queue.takeFirst();
        if (seen.contains(s->id))
            continue;
        seen.insert(s->id);
        result.append(s);
        queue.append(childShapes(s));
    }
    return result;
}

std::unique_ptr<Shape> Board::makeShape(const QString &type) const
{
    if (!family_)
        return nullptr;
    for (const ShapeType &shapeType : family_->shapes) {
        if (shapeType.name == type)
            return shapeType.make();
    }
    return nullptr;
}

std::unique_ptr<Link> Board::makeLink(const QString &type, bool *allowSelf, bool *allowMulti) const
{
    if (!family_)
        return nullptr;
    for (const LinkType &linkType : family_->links) {
        if (linkType.name == type) {
            *allowSelf = linkType.allowSelf;
            *allowMulti = linkType.allowMulti;
            return linkType.make();
        }
    }
    return nullptr;
}

QString Board::newId(QChar prefix)
{
    QString id;
    do {
        id = prefix + QString::number(nextId_++);
    } while (shapeIndex_.contains(id) || linkIndex_.contains(id));
    return id;
}

Shape *Board::addShape(const QString &requestedType, const QJsonObject &props)
{
    // Saved documents can outlive a family's dictionary: an unknown type
    // falls back to the default shape, so that old boards still open.
    const QString type = requestedType.isEmpty() || !makeShape(requestedType)
                             ? (family_ ? family_->defaultShape : QString())
                             : requestedType;
    std::unique_ptr<Shape> created = makeShape(type);
    if (!created)
        return nullptr;
    created->load(props);
    created->type = type;
    if (created->id.isEmpty() || shapeIndex_.contains(created->id) || linkIndex_.contains(created->id))
        created->id = newId(QLatin1Char('s'));
    Shape *s = created.get();
    shapes_.push_back(std::move(created));
    shapeIndex_.insert(s->id, s);
    measureShape(*s, &props);
    s->onAdded(*this);
    ++version_;
    return s;
}

Link *Board::addLink(const QString &requestedType, const QString &from, const QString &to, const QJsonObject &props)
{
    if (!shape(from) || !shape(to))
        return nullptr;
    bool allowSelf = false, allowMulti = false;
    QString type = requestedType.isEmpty() ? (family_ ? family_->defaultLink : QString()) : requestedType;
    std::unique_ptr<Link> created = makeLink(type, &allowSelf, &allowMulti);
    if (!created && family_) {
        type = family_->defaultLink;
        created = makeLink(type, &allowSelf, &allowMulti);
    }
    if (!created)
        return nullptr;
    if (from == to && !allowSelf)
        return nullptr;
    if (!allowMulti) {
        for (const auto &l : links_) {
            if (l->from == from && l->to == to)
                return l.get();
        }
    }
    created->load(props);
    created->type = type;
    created->from = from;
    created->to = to;
    if (created->id.isEmpty() || linkIndex_.contains(created->id) || shapeIndex_.contains(created->id))
        created->id = newId(QLatin1Char('l'));
    Link *l = created.get();
    links_.push_back(std::move(created));
    linkIndex_.insert(l->id, l);
    ++version_;
    return l;
}

std::unique_ptr<Shape> Board::instantiate(const QString &type, const QJsonObject &props) const
{
    std::unique_ptr<Shape> created = makeShape(type.isEmpty() && family_ ? family_->defaultShape : type);
    if (!created)
        return nullptr;
    created->load(props);
    created->type = type;
    diagram::measureShape(*this, *created, &props);
    return created;
}

bool Board::allowDeletion() const
{
    return !family_ || family_->allowDeletion;
}

bool Board::removeShape(const QString &id)
{
    if (!allowDeletion())
        return false;
    Shape *removed = shape(id);
    if (!removed)
        return false;
    QList<const Link *> touching;
    for (const auto &l : links_) {
        if (l->from == id || l->to == id)
            touching.append(l.get());
    }
    emit removing({removed}, touching);
    // Children outlive the container that held them: they go up to its own
    // parent rather than pointing at a shape that no longer exists.
    const QString inherited = removed->parentBoundary;
    for (const auto &s : shapes_) {
        if (s.get() == removed || s->parentBoundary.isEmpty())
            continue;
        if (s->parentBoundary == removed->id || (!removed->key.isEmpty() && s->parentBoundary == removed->key))
            s->parentBoundary = inherited;
    }
    for (auto it = links_.begin(); it != links_.end();) {
        if ((*it)->from == id || (*it)->to == id) {
            linkSelection_.remove((*it)->id);
            linkIndex_.remove((*it)->id);
            it = links_.erase(it);
        } else {
            ++it;
        }
    }
    selection_.remove(id);
    shapeIndex_.remove(id);
    shapes_.erase(
        std::find_if(shapes_.begin(), shapes_.end(), [removed](const auto &s) { return s.get() == removed; }));
    ++version_;
    return true;
}

bool Board::removeLink(const QString &id)
{
    if (!allowDeletion())
        return false;
    Link *removed = link(id);
    if (!removed)
        return false;
    emit removing({}, {removed});
    linkSelection_.remove(id);
    linkIndex_.remove(id);
    links_.erase(std::find_if(links_.begin(), links_.end(), [removed](const auto &l) { return l.get() == removed; }));
    ++version_;
    return true;
}

void Board::bringToFront(const QString &id)
{
    auto it = std::find_if(shapes_.begin(), shapes_.end(), [&id](const auto &s) { return s->id == id; });
    if (it == shapes_.end() || it + 1 == shapes_.end())
        return;
    std::unique_ptr<Shape> moved = std::move(*it);
    shapes_.erase(it);
    shapes_.push_back(std::move(moved));
}

void Board::clear()
{
    shapes_.clear();
    links_.clear();
    shapeIndex_.clear();
    linkIndex_.clear();
    selection_.clear();
    linkSelection_.clear();
    ++version_;
}

void Board::reindex()
{
    shapeIndex_.clear();
    linkIndex_.clear();
    for (const auto &s : shapes_)
        shapeIndex_.insert(s->id, s.get());
    for (const auto &l : links_)
        linkIndex_.insert(l->id, l.get());
}

void Board::measureShape(Shape &shape, const QJsonObject *props) const
{
    diagram::measureShape(*this, shape, props);
}

void Board::measureAll()
{
    for (const auto &s : shapes_)
        measureShape(*s);
}

void Board::containShape(Shape &shape, bool growParent)
{
    if (!shape.staysInsideParent() || shape.parentBoundary.isEmpty())
        return;
    Shape *parent = shapeByReference(shape.parentBoundary);
    if (!parent)
        return;
    std::optional<QRectF> bounds = parent->childBounds(*this);
    if (!bounds)
        return;
    if (growParent && parent->resizable()) {
        const qreal requiredWidth = std::max(parent->w, shape.x + shape.w - parent->x + 12);
        const qreal requiredHeight = std::max(parent->h, shape.y + shape.h - parent->y + 12);
        if (requiredWidth > parent->w || requiredHeight > parent->h) {
            parent->resizeTo(requiredWidth, requiredHeight, *this);
            containShape(*parent, true);
            bounds = parent->childBounds(*this);
        }
    }
    shape.x = clamp(shape.x, bounds->left(), std::max(bounds->left(), bounds->right() - shape.w));
    shape.y = clamp(shape.y, bounds->top(), std::max(bounds->top(), bounds->bottom() - shape.h));
}

Shape *Board::containerAt(QPointF point, const Shape *moved) const
{
    QSet<QString> forbidden;
    if (moved) {
        forbidden.insert(moved->id);
        for (const Shape *descendant : descendantShapes(moved))
            forbidden.insert(descendant->id);
    }
    Shape *best = nullptr;
    for (const auto &s : shapes_) {
        if (!s->isCreationContainer() || forbidden.contains(s->id))
            continue;
        if (point.x() < s->x || point.x() > s->x + s->w || point.y() < s->y || point.y() > s->y + s->h)
            continue;
        if (!best || s->w * s->h < best->w * best->h)
            best = s.get();
    }
    return best;
}

std::optional<Shape *> Board::dropDecision(const Shape &shape, const QSet<QString> &moving) const
{
    Shape *container = containerAt(QPointF(shape.cx(), shape.cy()), &shape);
    const QString current = shape.parentBoundary;
    const QString reference = container ? (container->key.isEmpty() ? container->id : container->key) : QString();
    if (reference == current)
        return std::nullopt;
    // Shapes dragged together keep their arrangement: a gesture never
    // rewrites membership inside its own moving set.
    if (container && moving.contains(container->id))
        return std::nullopt;
    return container;
}

Shape *Board::createShapeAt(QPointF center, const QString &type, const QJsonObject &props, bool edit)
{
    const QByteArray before = snapshot();
    QJsonObject all = creationShapeProps;
    for (auto it = props.begin(); it != props.end(); ++it)
        all.insert(it.key(), it.value());
    all.insert(QStringLiteral("x"), center.x());
    all.insert(QStringLiteral("y"), center.y());
    Shape *created = addShape(type, all);
    if (!created)
        return nullptr;
    // addShape measured the real content: centering is exact.
    created->x = center.x() - created->w / 2;
    created->y = center.y() - created->h / 2;
    containShape(*created, true);
    selection_ = {created->id};
    linkSelection_.clear();
    emit elementsCreated({created->id}, {});
    if (edit) {
        // If the shape opens an editor, the creation and the first edit make
        // one step of history, recorded when the edit ends.
        foldedEditBase_ = before;
        created->onDoubleClick(*this);
        if (!foldedEditBase_.isEmpty()) {
            foldedEditBase_.clear();
            pushHistory(before);
        }
    } else {
        pushHistory(before);
    }
    emit selectionChanged();
    emit changed();
    return created;
}

Link *Board::connect(const QString &from, const QString &to, const QJsonObject &props)
{
    QJsonObject all = creationLinkProps;
    for (auto it = props.begin(); it != props.end(); ++it)
        all.insert(it.key(), it.value());
    return addLink(family_ ? family_->defaultLink : QString(), from, to, all);
}

// ---------------------------------------------------------------------------
// Selection

void Board::clearSelection()
{
    if (selection_.isEmpty() && linkSelection_.isEmpty())
        return;
    selection_.clear();
    linkSelection_.clear();
    emit selectionChanged();
}

void Board::selectShape(const QString &id, bool add)
{
    if (!add) {
        selection_.clear();
        linkSelection_.clear();
    }
    if (shape(id))
        selection_.insert(id);
    emit selectionChanged();
}

void Board::selectLink(const QString &id, bool add)
{
    if (!add) {
        selection_.clear();
        linkSelection_.clear();
    }
    if (link(id))
        linkSelection_.insert(id);
    emit selectionChanged();
}

void Board::deselect(const QString &id)
{
    if (selection_.remove(id) || linkSelection_.remove(id))
        emit selectionChanged();
}

void Board::selectAll()
{
    selection_.clear();
    for (const auto &s : shapes_)
        selection_.insert(s->id);
    emit selectionChanged();
}

void Board::selectInRect(const QRectF &rect, bool add)
{
    if (!add) {
        selection_.clear();
        linkSelection_.clear();
    }
    for (const auto &s : shapes_) {
        if (rect.intersects(s->rect()))
            selection_.insert(s->id);
    }
    for (const auto &l : links_) {
        if (selection_.contains(l->from) && selection_.contains(l->to))
            linkSelection_.insert(l->id);
    }
    emit selectionChanged();
}

Shape *Board::selectedShape() const
{
    return selection_.size() == 1 && linkSelection_.isEmpty() ? shape(*selection_.begin()) : nullptr;
}

Link *Board::selectedLink() const
{
    return linkSelection_.size() == 1 && selection_.isEmpty() ? link(*linkSelection_.begin()) : nullptr;
}

// ---------------------------------------------------------------------------
// History

QByteArray Board::snapshot(bool routes) const
{
    // Sorted by id, so that bringing a clicked shape to the top never counts
    // as a change.
    QJsonArray shapes, links;
    for (const auto &s : shapes_)
        shapes.append(s->toJson());
    for (const auto &l : links_) {
        QJsonObject json = l->toJson();
        if (!routes)
            json.remove(QStringLiteral("spline"));
        links.append(json);
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("shapes"), sortedById(shapes)},
                                     {QStringLiteral("links"), sortedById(links)}})
        .toJson(QJsonDocument::Compact);
}

void Board::pushHistory(const QByteArray &before)
{
    if (before.isEmpty() || snapshot() == before)
        return;
    undo_.push_back(before);
    if (undo_.size() > historyLimit)
        undo_.erase(undo_.begin());
    redo_.clear();
    ++version_;
    emit geometrySettled();
    emit changed();
}

void Board::applySnapshot(const QByteArray &data)
{
    const QJsonObject json = QJsonDocument::fromJson(data).object();
    shapes_.clear();
    links_.clear();
    shapeIndex_.clear();
    linkIndex_.clear();
    for (const QJsonValue &s : json.value(QStringLiteral("shapes")).toArray())
        addShape(s.toObject().value(QStringLiteral("type")).toString(), s.toObject());
    for (const QJsonValue &l : json.value(QStringLiteral("links")).toArray()) {
        const QJsonObject props = l.toObject();
        addLink(props.value(QStringLiteral("type")).toString(), props.value(QStringLiteral("from")).toString(),
                props.value(QStringLiteral("to")).toString(), props);
    }
    for (const QString &id : QSet<QString>(selection_)) {
        if (!shape(id))
            selection_.remove(id);
    }
    for (const QString &id : QSet<QString>(linkSelection_)) {
        if (!link(id))
            linkSelection_.remove(id);
    }
    resolveLabels();
    ++version_;
    emit selectionChanged();
    emit geometrySettled();
    emit changed();
}

void Board::undo()
{
    if (undo_.empty())
        return;
    redo_.push_back(snapshot());
    const QByteArray previous = undo_.back();
    undo_.pop_back();
    applySnapshot(previous);
}

void Board::redo()
{
    if (redo_.empty())
        return;
    undo_.push_back(snapshot());
    const QByteArray next = redo_.back();
    redo_.pop_back();
    applySnapshot(next);
}

void Board::clearHistory()
{
    undo_.clear();
    redo_.clear();
}

void Board::edit(const std::function<void()> &change)
{
    const QByteArray before = snapshot();
    change();
    resolveLabels();
    pushHistory(before);
    emit changed();
}

// ---------------------------------------------------------------------------
// Clipboard

QJsonObject Board::selectionJson() const
{
    QJsonArray shapes, links;
    for (const auto &s : shapes_) {
        if (selection_.contains(s->id))
            shapes.append(s->toJson());
    }
    // Links selected, and links whose both ends are selected.
    for (const auto &l : links_) {
        if (linkSelection_.contains(l->id) || (selection_.contains(l->from) && selection_.contains(l->to)))
            links.append(l->toJson());
    }
    return QJsonObject{{QStringLiteral("format"), QStringLiteral("duet-diagram-selection")},
                       {QStringLiteral("family"), family_ ? family_->id : QString()},
                       {QStringLiteral("shapes"), shapes},
                       {QStringLiteral("links"), links}};
}

bool Board::copySelection()
{
    if (!hasSelection())
        return false;
    const QJsonObject json = selectionJson();
    if (json.value(QStringLiteral("shapes")).toArray().isEmpty())
        return false;
    clipboardShapes_ = json.value(QStringLiteral("shapes")).toArray();
    clipboardLinks_ = json.value(QStringLiteral("links")).toArray();
    clipboardOffset_ = 24;
    return true;
}

bool Board::cutSelection()
{
    if (!allowDeletion() || !copySelection())
        return false;
    deleteSelection();
    return true;
}

void Board::addClipboardContent(const QJsonArray &shapes, const QJsonArray &links, QPointF offset)
{
    // A copy is a new element: carrying the source `key` over would merge
    // both into one element on save.
    QSet<QString> taken;
    for (const auto &s : shapes_) {
        if (!s->key.isEmpty())
            taken.insert(s->key);
    }
    static const QRegularExpression copySuffix(QStringLiteral("_copy(_\\d+)?$"));
    const auto freshKey = [&taken](const QString &key) {
        QString root = key;
        root.remove(copySuffix);
        QString candidate = root + QStringLiteral("_copy");
        for (int index = 2; taken.contains(candidate); ++index)
            candidate = root + QStringLiteral("_copy_") + QString::number(index);
        taken.insert(candidate);
        return candidate;
    };
    QHash<QString, QString> idMap;
    QHash<QString, Shape *> byReference;
    QList<std::pair<QJsonObject, Shape *>> pasted;
    QStringList createdShapes, createdLinks;
    selection_.clear();
    linkSelection_.clear();
    for (const QJsonValue &value : shapes) {
        QJsonObject props = value.toObject();
        const QString oldId = props.value(QStringLiteral("id")).toString();
        const QString oldKey = props.value(QStringLiteral("key")).toString();
        props.remove(QStringLiteral("id"));
        props.insert(QStringLiteral("x"), props.value(QStringLiteral("x")).toDouble() + offset.x());
        props.insert(QStringLiteral("y"), props.value(QStringLiteral("y")).toDouble() + offset.y());
        if (!oldKey.isEmpty())
            props.insert(QStringLiteral("key"), freshKey(oldKey));
        props.remove(QStringLiteral("parentBoundary"));
        Shape *s = addShape(props.value(QStringLiteral("type")).toString(), props);
        if (!s)
            continue;
        idMap.insert(oldId, s->id);
        if (!oldId.isEmpty())
            byReference.insert(oldId, s);
        if (!oldKey.isEmpty())
            byReference.insert(oldKey, s);
        pasted.append({value.toObject(), s});
        selection_.insert(s->id);
        createdShapes.append(s->id);
    }
    // A copied child follows its copied frame; one whose frame stayed behind
    // stays in the original.
    for (const auto &[original, s] : pasted) {
        const QString parent = original.value(QStringLiteral("parentBoundary")).toString();
        if (parent.isEmpty())
            continue;
        if (Shape *copied = byReference.value(parent))
            s->parentBoundary = copied->key.isEmpty() ? copied->id : copied->key;
        else
            s->parentBoundary = parent;
        containShape(*s);
    }
    for (const QJsonValue &value : links) {
        QJsonObject props = value.toObject();
        const QString from = idMap.value(props.value(QStringLiteral("from")).toString());
        const QString to = idMap.value(props.value(QStringLiteral("to")).toString());
        if (from.isEmpty() || to.isEmpty())
            continue;
        props.remove(QStringLiteral("id"));
        props.remove(QStringLiteral("spline"));
        props.remove(QStringLiteral("key"));
        if (Link *l = addLink(props.value(QStringLiteral("type")).toString(), from, to, props))
            createdLinks.append(l->id);
    }
    emit elementsCreated(createdShapes, createdLinks);
}

bool Board::pasteClipboard()
{
    if (clipboardShapes_.isEmpty())
        return false;
    const QByteArray before = snapshot();
    addClipboardContent(clipboardShapes_, clipboardLinks_, QPointF(clipboardOffset_, clipboardOffset_));
    clipboardOffset_ += 24; // successive pastes are staggered
    resolveLabels();
    pushHistory(before);
    emit selectionChanged();
    emit changed();
    return true;
}

bool Board::pasteJson(const QJsonObject &json, QPointF center)
{
    const QJsonArray shapes = json.value(QStringLiteral("shapes")).toArray();
    if (shapes.isEmpty())
        return false;
    QRectF box;
    for (const QJsonValue &value : shapes) {
        const QJsonObject s = value.toObject();
        const QRectF r(s.value(QStringLiteral("x")).toDouble(), s.value(QStringLiteral("y")).toDouble(),
                       s.value(QStringLiteral("w")).toDouble(), s.value(QStringLiteral("h")).toDouble());
        box = box.isNull() ? r : box.united(r);
    }
    const QByteArray before = snapshot();
    addClipboardContent(shapes, json.value(QStringLiteral("links")).toArray(), center - box.center());
    resolveLabels();
    pushHistory(before);
    emit selectionChanged();
    emit changed();
    return true;
}

void Board::deleteSelection()
{
    if (!allowDeletion() || !hasSelection())
        return;
    const QByteArray before = snapshot();
    for (const QString &id : QSet<QString>(selection_))
        removeShape(id);
    for (const QString &id : QSet<QString>(linkSelection_))
        removeLink(id);
    selection_.clear();
    linkSelection_.clear();
    resolveLabels();
    pushHistory(before);
    emit selectionChanged();
    emit changed();
}

void Board::duplicateSelection()
{
    if (!copySelection())
        return;
    pasteClipboard();
}

// ---------------------------------------------------------------------------
// Appearance

bool Board::paintSelection(const QString &fill)
{
    if (!fill.isEmpty() && !isHexColor(fill))
        return false;
    const QByteArray before = snapshot();
    for (const QString &id : selection_) {
        if (Shape *s = shape(id))
            s->customFill = fill.toLower();
    }
    pushHistory(before);
    emit changed();
    return true;
}

bool Board::styleSelectedLinks(const QJsonObject &patch)
{
    const QByteArray before = snapshot();
    for (const QString &id : linkSelection_) {
        Link *l = link(id);
        if (!l)
            continue;
        if (patch.contains(QStringLiteral("customLineStyle"))) {
            const QString line = patch.value(QStringLiteral("customLineStyle")).toString();
            l->customLineStyle =
                line == QLatin1String("solid") || line == QLatin1String("dashed") || line == QLatin1String("dotted")
                    ? line
                    : QString();
        }
        if (patch.contains(QStringLiteral("customColor"))) {
            const QString color = patch.value(QStringLiteral("customColor")).toString();
            l->customColor = isHexColor(color) ? color.toLower() : QString();
        }
        if (patch.contains(QStringLiteral("customWidth"))) {
            const QJsonValue width = patch.value(QStringLiteral("customWidth"));
            if (width.isDouble() && width.toDouble() >= LinkWidthMin && width.toDouble() <= LinkWidthMax)
                l->customWidth = width.toDouble();
            else
                l->customWidth.reset();
        }
    }
    pushHistory(before);
    emit changed();
    return true;
}

// ---------------------------------------------------------------------------
// Serialization

QJsonObject Board::toJson() const
{
    QJsonArray shapes, links;
    for (const auto &s : shapes_)
        shapes.append(s->toJson());
    for (const auto &l : links_)
        links.append(l->toJson());
    return QJsonObject{{QStringLiteral("shapes"), shapes}, {QStringLiteral("links"), links}};
}

void Board::fromJson(const QJsonObject &json)
{
    clear();
    for (const QJsonValue &s : json.value(QStringLiteral("shapes")).toArray())
        addShape(s.toObject().value(QStringLiteral("type")).toString(), s.toObject());
    for (const QJsonValue &l : json.value(QStringLiteral("links")).toArray()) {
        const QJsonObject props = l.toObject();
        addLink(props.value(QStringLiteral("type")).toString(), props.value(QStringLiteral("from")).toString(),
                props.value(QStringLiteral("to")).toString(), props);
    }
    resolveLabels();
    clearHistory();
    emit changed();
}

QRectF Board::contentBounds() const
{
    QRectF box;
    const auto merge = [&box](const QRectF &r) {
        if (!r.isValid() && r.isNull())
            return;
        box = box.isNull() ? r : box.united(r);
    };
    for (const auto &s : shapes_)
        merge(s->bounds(*this).value_or(s->rect()));
    for (const auto &l : links_) {
        if (const std::optional<QRectF> b = l->bounds(*this))
            merge(b->isNull() ? QRectF(b->topLeft(), QSizeF(0.01, 0.01)) : *b);
    }
    return box;
}

// ---------------------------------------------------------------------------
// Text editing

void Board::beginTextEdit(Shape *shape, const TextEditSpec &spec)
{
    if (textEditor_)
        textEditor_(shape, nullptr, spec);
}

void Board::beginTextEdit(Link *link, const TextEditSpec &spec)
{
    if (textEditor_)
        textEditor_(nullptr, link, spec);
}

void Board::editLinkLabel(Link &l)
{
    const auto &o = style_.linkLabel;
    TextEditSpec spec;
    spec.get = [&l] { return l.label; };
    spec.set = [&l](const QString &value) { l.label = value; };
    spec.color = o.color;
    spec.font = Font(o.fontPx);
    spec.lineHeight = o.fontPx + 4;
    spec.align = Align::Center;
    spec.rect = [this, &l, o] {
        const std::optional<Route> r = l.route(*this);
        if (!r)
            return QRectF();
        const QPointF m = l.labelPos(*r, *this);
        const QStringList lines = (l.label.isEmpty() ? QStringLiteral("label") : l.label).split(QLatin1Char('\n'));
        qreal ww = 50;
        for (const QString &line : lines)
            ww = std::max(ww, text::width(Font(o.fontPx), line.isEmpty() ? QStringLiteral(" ") : line) + o.padX * 2);
        const qreal hh = std::max(o.fontPx + o.padY * 2, lines.size() * (o.fontPx + 2) + o.padY * 2);
        return QRectF(m.x() - ww / 2, m.y() - hh / 2, ww, hh);
    };
    beginTextEdit(&l, spec);
}

QByteArray Board::takeFoldedEditBase()
{
    QByteArray base = foldedEditBase_;
    foldedEditBase_.clear();
    return base;
}

void Board::resolveLabels()
{
    if (family_ && family_->resolveLabels)
        family_->resolveLabels(*this);
}

void Board::touch()
{
    ++version_;
    emit changed();
}

} // namespace diagram
