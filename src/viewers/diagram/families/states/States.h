#pragma once

#include "viewers/diagram/Shape.h"

// State diagrams, ported from sidecar's state-diagram.js: states with their
// description lines, composite states (Graphviz clusters holding their
// children through `parentBoundary`), the UML pseudo-states (initial, final,
// choice, fork, join) and the transition, with its self-loops.
namespace diagram::states {

// What every state element shares: the id it has in the source, within the
// composite that declares it (the board key is scoped: "Active.Idle").
class StateElement : public Shape {
public:
    StateElement(const QString &type, qreal width, qreal height);

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    // A child is kept inside its composite, as Mermaid scopes it there.
    bool staysInsideParent() const override { return true; }
    // The text the source writes for it (`state "text" as id`); empty for
    // the pseudo-states, whose symbol is their meaning.
    virtual QString sourceText() const { return {}; }

    QString sourceKey;
};

class StateShape : public StateElement {
public:
    StateShape();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    void resizeTo(qreal width, qreal height, const Board &board) override;
    void onDoubleClick(Board &board) override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;
    QString displayText() const override { return text; }
    QString sourceText() const override { return text; }
    QList<Property> properties() const override;
    QVariant propertyValue(const QString &id) const override;
    void setPropertyValue(const QString &id, const QVariant &value) override;

    QString text;     // the display name
    QStringList desc; // internal description lines ("id : desc")
    QString fill = QStringLiteral("#ffffff");

private:
    void wrapLines();

    QStringList titleLines_;
    QStringList lines_;
};

// A composite state. Its interior does not hit-test: a double-click there
// creates a child, while the border and the title stay selectable.
class StateContainerShape : public StateElement {
public:
    StateContainerShape();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    std::optional<QSizeF> resizeMinimum(const Board &board) const override;
    void resizeTo(qreal width, qreal height, const Board &board) override;
    bool hitTest(QPointF point) const override;
    void onDoubleClick(Board &board) override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;
    bool drawBehindLinks() const override { return true; }
    bool movesChildren() const override { return true; }
    bool isCreationContainer() const override { return true; }
    std::optional<QRectF> childBounds(const Board &board) const override;
    bool isGraphvizCluster() const override { return true; }
    QString layoutLabel() const override { return text; }
    qreal layoutLabelFontPx() const override;
    std::optional<QSizeF> layoutMinimumSize() const override;
    QString displayText() const override { return text; }
    QString sourceText() const override { return text; }

    QString text;

private:
    qreal labelWidth_ = 0;
    // The floor of its size: a new, empty composite's, or only its title
    // once it holds children (their layout then gives it its size).
    QSizeF minimum_{300, 210};
    // The size measure() last left: a different one was set from outside
    // (the layout).
    std::optional<QSizeF> measured_;
};

// Small round pseudo-states: the initial dot and the final bullseye.
class RoundShape : public StateElement {
public:
    RoundShape(const QString &type, qreal size);

    bool resizable() const override { return false; }
    bool hitTest(QPointF point) const override;
    QPointF borderPoint(QPointF toward) const override;
    QString displayText() const override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;
};

// The <<choice>> pseudo-state: a diamond, with an optional condition.
class ChoiceShape : public StateElement {
public:
    ChoiceShape();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    void resizeTo(qreal width, qreal height, const Board &board) override;
    void onDoubleClick(Board &board) override;
    QPointF borderPoint(QPointF toward) const override;
    QStringList graphvizNodeAttributes(const Board &board, const QString &rankdir, qreal width,
                                       qreal height) const override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;
    QString displayText() const override { return text; }
    QString sourceText() const override { return text; }

    QString text;

    // The wrapped condition, as drawn (tests).
    const QStringList &lines() const { return lines_; }

private:
    QPainterPath outline() const;

    QStringList lines_;
};

// UML fork and join share the synchronization bar; their types stay distinct
// so that the sources do not turn them into one another.
class ForkJoinShape : public StateElement {
public:
    explicit ForkJoinShape(const QString &type);

    bool resizable() const override { return false; }
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;
    QString displayText() const override;
    QList<Property> properties() const override;
    QVariant propertyValue(const QString &id) const override;
    void setPropertyValue(const QString &id, const QVariant &value) override;
};

class TransitionLink : public Link {
public:
    TransitionLink();

    // Self-transitions loop out of the shape's top-right corner.
    std::optional<Route> route(const Board &board) const override;
    QPointF labelPos(const Route &route, const Board &board) const override;
    void draw(QPainter &painter, const Board &board, bool selected) const override;
    // Transitions almost always carry a label: parallels keep a pill's
    // height between them, not just a stroke's.
    qreal parallelSpread(const Board &board) const override;

    // A self-loop's control radius and its label's offset, growing with the
    // loops of the same state before it.
    qreal selfOffset(const Board &board) const;

    // The place resolveLabels() chose for the label, valid while both ends
    // stay where they were.
    struct Placement {
        QPointF point;
        QPointF fromAnchor;
        QPointF toAnchor;
    };
    mutable std::optional<Placement> placement;

private:
    std::optional<QPointF> validPlacement(const Board &board) const;
};

// The family's last layout pass (sidecar's resolveStateLabels): a label
// never leaves its transition; among points sampled along it, the one that
// overlaps the states and the other labels least is kept.
void resolveLabels(Board &board);

} // namespace diagram::states
