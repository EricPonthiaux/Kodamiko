// SPDX-License-Identifier: GPL-3.0-only

// The canvas's menus: the context menu, its style and property submenus,
// and the choice of the element or the relation to create.
#include "Board.h"
#include "Canvas.h"
#include "Family.h"
#include "ui/ContextMenus.h"

#include <QMenu>
#include <QPixmap>

namespace diagram {

namespace {

// A menu whose actions show the element or relation they create: the style
// sheet draws its icons larger than a menu's 16 px, where the shapes are
// specks that cannot be told apart.
void showPresetIcons(QMenu &menu)
{
    menu.setObjectName(QStringLiteral("diagramPresets"));
}

} // namespace

void Canvas::chooseElement(QPointF screen, QPointF point, const QString &from)
{
    const Family *family = board_->family();
    if (!family)
        return;
    if (family->elements.isEmpty()) {
        createElement(ElementPreset{}, point, from);
        return;
    }
    QMenu menu(this);
    showPresetIcons(menu);
    menu.setToolTipsVisible(true);
    const Style &style = board_->style();
    for (const ElementPreset &preset : family->elements) {
        QAction *action = menu.addAction(presetIcon(*family, preset, style), preset.label);
        if (!preset.help.isEmpty())
            action->setToolTip(preset.help);
        connect(action, &QAction::triggered, this, [this, preset, point, from] { createElement(preset, point, from); });
    }
    ContextMenus::exec(menu, *this, screen.toPoint());
}

void Canvas::createElement(const ElementPreset &preset, QPointF point, const QString &from)
{
    const Family *family = board_->family();
    if (preset.trace) {
        // A two-point element (an arrow): drawn by a press-drag-release.
        trace_ = Trace{preset.shapeType, preset.props};
        setCursor(Qt::CrossCursor);
        emit message(tr("Drag to draw it"));
        return;
    }
    QJsonObject props = preset.props;
    // Created inside a frame, it joins it.
    if (family && family->nestedCreation) {
        if (const Shape *parent = board_->containerAt(point))
            props.insert(QStringLiteral("parentBoundary"), parent->key.isEmpty() ? parent->id : parent->key);
    }
    if (from.isEmpty()) {
        board_->createShapeAt(point, preset.shapeType, props, true);
        return;
    }
    // At the end of a link: the shape and its link are one step.
    const QByteArray before = board_->snapshot();
    Shape *created = nullptr;
    {
        const QSignalBlocker blocker(board_);
        QJsonObject all = board_->creationShapeProps;
        for (auto it = props.begin(); it != props.end(); ++it)
            all.insert(it.key(), it.value());
        created = board_->addShape(preset.shapeType, all);
        if (created) {
            created->x = point.x() - created->w / 2;
            created->y = point.y() - created->h / 2;
            board_->containShape(*created, true);
            board_->connect(from, created->id);
        }
    }
    if (!created)
        return;
    board_->selectShape(created->id);
    emit board_->elementsCreated({created->id}, {});
    board_->resolveLabels();
    board_->pushHistory(before);
}

void Canvas::chooseRelation(QPointF screen, const QString &from, const QString &to, const QJsonObject &portProps)
{
    const Family *family = board_->family();
    QMenu menu(this);
    showPresetIcons(menu);
    menu.setToolTipsVisible(true);
    const Shape *a = board_->shape(from);
    const Shape *b = board_->shape(to);
    QAction *title = menu.addAction(
        QStringLiteral("%1 → %2").arg(a ? a->displayText() : QString(), b ? b->displayText() : QString()));
    title->setEnabled(false);
    menu.addSeparator();
    const QByteArray before = dragBase_.isEmpty() ? board_->snapshot() : dragBase_;
    for (const RelationPreset &preset : family->relations) {
        QAction *action = menu.addAction(relationIcon(*family, preset, board_->style()), preset.label);
        if (!preset.help.isEmpty())
            action->setToolTip(preset.help);
        connect(action, &QAction::triggered, this, [this, preset, from, to, portProps, before] {
            QJsonObject props = preset.props;
            for (auto it = portProps.begin(); it != portProps.end(); ++it)
                props.insert(it.key(), it.value());
            if (const Link *l = board_->connect(from, to, props)) {
                emit board_->elementsCreated({}, {l->id});
                board_->selectLink(l->id);
            }
            board_->resolveLabels();
            board_->pushHistory(before);
        });
    }
    ContextMenus::exec(menu, *this, screen.toPoint());
}

void Canvas::addFillMenu(QMenu *menu)
{
    QMenu *fill = menu->addMenu(tr("Color"));
    for (const QString &color : swatches()) {
        QPixmap swatch(16, 16);
        swatch.fill(QColor::fromString(color));
        const QAction *action = fill->addAction(QIcon(swatch), color);
        connect(action, &QAction::triggered, this, [this, color] { board_->paintSelection(color); });
    }
    fill->addSeparator();
    connect(fill->addAction(tr("Default Color")), &QAction::triggered, this, [this] { board_->paintSelection({}); });
}

void Canvas::addLinkStyleMenu(QMenu *menu)
{
    QMenu *line = menu->addMenu(tr("Line Style"));
    const QList<std::pair<QString, QString>> styles{{QString(), tr("Default")},
                                                    {QStringLiteral("solid"), tr("Solid")},
                                                    {QStringLiteral("dashed"), tr("Dashed")},
                                                    {QStringLiteral("dotted"), tr("Dotted")}};
    for (const auto &[value, label] : styles) {
        connect(line->addAction(label), &QAction::triggered, this,
                [this, value] { board_->styleSelectedLinks(QJsonObject{{QStringLiteral("customLineStyle"), value}}); });
    }
    QMenu *color = menu->addMenu(tr("Line Color"));
    for (const QString &value : lineColors()) {
        QPixmap swatch(16, 16);
        swatch.fill(QColor::fromString(value));
        connect(color->addAction(QIcon(swatch), value), &QAction::triggered, this,
                [this, value] { board_->styleSelectedLinks(QJsonObject{{QStringLiteral("customColor"), value}}); });
    }
    color->addSeparator();
    connect(color->addAction(tr("Default Color")), &QAction::triggered, this,
            [this] { board_->styleSelectedLinks(QJsonObject{{QStringLiteral("customColor"), QString()}}); });
    QMenu *width = menu->addMenu(tr("Line Width"));
    connect(width->addAction(tr("Default")), &QAction::triggered, this,
            [this] { board_->styleSelectedLinks(QJsonObject{{QStringLiteral("customWidth"), QJsonValue::Null}}); });
    for (int value : {1, 2, 3, 4, 6, 8}) {
        connect(width->addAction(QString::number(value)), &QAction::triggered, this,
                [this, value] { board_->styleSelectedLinks(QJsonObject{{QStringLiteral("customWidth"), value}}); });
    }
}

void Canvas::addPropertyMenus(QMenu *menu, const QList<Property> &properties, PropertyTarget target)
{
    for (const Property &property : properties) {
        if (property.kind == Property::Kind::Choice)
            addChoiceMenu(menu, property, target);
        else if (property.kind == Property::Kind::Bool)
            addToggleAction(menu, property, target);
    }
}

void Canvas::addChoiceMenu(QMenu *menu, const Property &property, PropertyTarget target)
{
    QMenu *sub = menu->addMenu(property.label);
    const QString current = selectedPropertyValue(property.id, target).toString();
    for (const auto &[value, label] : property.choices) {
        QAction *action = sub->addAction(label);
        action->setCheckable(true);
        action->setChecked(value == current);
        const QString id = property.id;
        connect(action, &QAction::triggered, this,
                [this, id, value, target] { setSelectionProperty(id, value, target); });
    }
}

void Canvas::addToggleAction(QMenu *menu, const Property &property, PropertyTarget target)
{
    QAction *action = menu->addAction(property.label);
    action->setCheckable(true);
    action->setChecked(selectedPropertyValue(property.id, target).toBool());
    const QString id = property.id;
    connect(action, &QAction::toggled, this,
            [this, id, target](bool checked) { setSelectionProperty(id, checked, target); });
}

// The property of the selected element, null when none is selected.
QVariant Canvas::selectedPropertyValue(const QString &id, PropertyTarget target) const
{
    switch (target) {
    case PropertyTarget::Links:
        if (const Link *l = board_->selectedLink())
            return l->propertyValue(id);
        return {};
    case PropertyTarget::Shapes:
        if (const Shape *s = board_->selectedShape())
            return s->propertyValue(id);
        return {};
    }
    return {};
}

void Canvas::setSelectionProperty(const QString &id, const QVariant &value, PropertyTarget target)
{
    board_->edit([&] {
        if (target == PropertyTarget::Links) {
            for (const QString &linkId : board_->linkSelection()) {
                if (Link *l = board_->link(linkId))
                    l->setPropertyValue(id, value);
            }
            return;
        }
        for (const QString &shapeId : board_->selection()) {
            if (Shape *s = board_->shape(shapeId)) {
                s->setPropertyValue(id, value);
                board_->measureShape(*s);
            }
        }
    });
}

void Canvas::showContextMenu(QPointF screen)
{
    const QPointF p = world(screen);
    QMenu menu(this);
    const Shape *s = shapeAt(p);
    const Link *l = s ? nullptr : linkAt(p);
    if (s && !board_->selection().contains(s->id))
        board_->selectShape(s->id);
    if (l && !board_->linkSelection().contains(l->id))
        board_->selectLink(l->id);
    if (s)
        addShapeActions(menu, *s);
    else if (l)
        addLinkActions(menu, *l);
    else
        addCanvasActions(menu, p);
    ContextMenus::exec(menu, *this, mapToGlobal(screen.toPoint()));
}

void Canvas::addShapeActions(QMenu &menu, const Shape &shape)
{
    connect(menu.addAction(tr("Edit Text")), &QAction::triggered, this, [this] { editSelection(); });
    if (board_->selection().size() == 1)
        addPropertyMenus(&menu, shape.properties(), PropertyTarget::Shapes);
    addFillMenu(&menu);
    menu.addSeparator();
    connect(menu.addAction(tr("Cut")), &QAction::triggered, this, &Canvas::cut);
    connect(menu.addAction(tr("Copy")), &QAction::triggered, this, &Canvas::copy);
    connect(menu.addAction(tr("Duplicate")), &QAction::triggered, this, [this] { board_->duplicateSelection(); });
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Delete"));
    remove->setEnabled(board_->allowDeletion());
    connect(remove, &QAction::triggered, this, &Canvas::deleteSelection);
}

void Canvas::addLinkActions(QMenu &menu, const Link &link)
{
    connect(menu.addAction(tr("Edit Label")), &QAction::triggered, this, [this] { editSelection(); });
    if (board_->linkSelection().size() == 1)
        addPropertyMenus(&menu, link.properties(), PropertyTarget::Links);
    addLinkStyleMenu(&menu);
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Delete"));
    remove->setEnabled(board_->allowDeletion());
    connect(remove, &QAction::triggered, this, &Canvas::deleteSelection);
}

void Canvas::addCanvasActions(QMenu &menu, QPointF point)
{
    const Family *family = board_->family();
    if (family && !family->elements.isEmpty()) {
        QMenu *add = menu.addMenu(tr("Add"));
        showPresetIcons(*add);
        for (const ElementPreset &preset : family->elements) {
            connect(add->addAction(presetIcon(*family, preset, board_->style()), preset.label), &QAction::triggered,
                    this, [this, preset, point] { createElement(preset, point, {}); });
        }
    } else if (family) {
        connect(menu.addAction(tr("Add")), &QAction::triggered, this,
                [this, point] { createElement(ElementPreset{}, point, {}); });
    }
    connect(menu.addAction(tr("Paste")), &QAction::triggered, this, &Canvas::paste);
    connect(menu.addAction(tr("Select All")), &QAction::triggered, this, [this] { board_->selectAll(); });
    menu.addSeparator();
    connect(menu.addAction(tr("Lay Out")), &QAction::triggered, this, [this] { arrange(true); });
    connect(menu.addAction(tr("Fit")), &QAction::triggered, this, [this] { fitView(true); });
}

} // namespace diagram
