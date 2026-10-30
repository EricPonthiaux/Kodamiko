// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "viewers/diagram/Shape.h"

#include <functional>

// Sequence diagrams, ported from sidecar's sequence-diagram.js: participants
// (a header box, or an actor's silhouette, over a dashed lifeline) and
// messages, horizontal segments between two lifelines at the lane of their
// rank in the conversation. Fragments (loop, alt, opt, par…) are not shapes:
// each message carries the stack of fragments it lies in, and the family
// draws their frames under everything.
namespace diagram::sequence {

class MessageLink;

// One level of the fragment stack a message lies in. Every message of a
// fragment carries a copy with the same id; the branch it lies in (`else`,
// `and`, `option`) is the branch keyword and label of its copy.
struct Fragment {
    QString id;
    QString kind; // loop | alt | opt | par | critical | break | rect | group
    QString label;
    int depth = 0;
    QString branchKeyword;
    QString branchLabel;

    QJsonObject toJson() const;
    static Fragment fromJson(const QJsonObject &json);
};

// The lane of the message of rank `order`: below the lowest header, one
// step per message, plus the room fragment headers and branches reserve.
qreal messageY(const Board &board, int order);
// The rank the next message takes.
int nextMessageOrder(const Board &board);

// Writes the fragment statements that lead from the stack `active` to the
// stack `next` (closing, opening, switching branch), and returns `next`.
// Mermaid and PlantUML differ only by indentation and Mermaid's implicit
// `and` branch of parallel fragments.
struct FragmentSyntax {
    QString indent;
    bool parallelBranches = false;
    std::function<QString(const QString &)> encode; // escapes the labels for the notation
};

QList<Fragment> syncFragments(QStringList &out,
                              const QList<Fragment> &active,
                              const QList<Fragment> &next,
                              const FragmentSyntax &syntax = {});

// Header + dashed lifeline. `kind` keeps the Mermaid/PlantUML participant
// kind for faithful round trips; actors are drawn as their UML silhouette.
class ParticipantShape : public Shape {
public:
    ParticipantShape();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    void resizeTo(qreal width, qreal height, const Board &board) override;

    bool showConnectionPortsOnHover() const override { return true; }

    QList<Port> connectionPorts(const Board &board) const override;
    bool connectionHitTest(const Board &board, QPointF point) const override;
    QPointF connectionPoint(const Board &board, QPointF from, QPointF pointer) const override;
    void onDoubleClick(Board &board) override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;
    std::optional<QRectF> bounds(const Board &board) const override;

    QString displayText() const override { return text; }

    QList<Property> properties() const override;
    QVariant propertyValue(const QString &propertyId) const override;
    void setPropertyValue(const QString &propertyId, const QVariant &value) override;

    bool isActor() const { return kind == QLatin1String("actor"); }

    QString text;
    // participant | actor, or a PlantUML kind (boundary, database…).
    QString kind = QStringLiteral("participant");
    QString fill = QStringLiteral("#ffffff");

private:
    qreal lifelineEnd(const Board &board) const;
    void drawActivations(QPainter &painter, const Board &board) const;
    // The UML silhouette with its text below, or the header box.
    void drawActor(QPainter &painter, const Style &style, const ShapeState &state) const;
    void drawHeader(QPainter &painter, const Style &style, const ShapeState &state) const;
    QColor headerFill(const Style &style) const;
    QColor headerInk(const Style &style) const;

    QStringList lines_;
};

// One message. Sequences need several messages between the same pair and
// self-messages: the family's link type allows both.
class MessageLink : public Link {
public:
    MessageLink();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    // The horizontal segment between the two lifelines at this message's
    // lane, or the loop of a self-message. A message drawn interactively has
    // no rank yet: it takes the next lane.
    std::optional<Route> route(const Board &board) const override;
    LabelBox labelBox(const Board &board) const override;
    QPointF labelPos(const Route &route, const Board &board) const override;
    bool hitTest(const Board &board, QPointF point, qreal tolerance) const override;
    LinkStyle drawStyle(const Board &board, bool selected) const override;
    void draw(QPainter &painter, const Board &board, bool selected) const override;
    void drawLabel(QPainter &painter, const Board &board, bool editing) const override;
    void onDoubleClick(Board &board) override;

    bool routesItself() const override { return true; }

    QList<Property> properties() const override;
    QVariant propertyValue(const QString &propertyId) const override;
    void setPropertyValue(const QString &propertyId, const QVariant &value) override;

    // Rank in the conversation (0-based); assigned on first routing when
    // missing.
    mutable std::optional<int> order;
    QString line = QStringLiteral("solid");     // solid | dashed
    QString head = QStringLiteral("arrow");     // arrow | open | cross | none
    QString startHead = QStringLiteral("none"); // none | arrow | open | cross
    QString activation;                         // "", "+" or "-"
    QList<Fragment> fragments;

private:
    // A head to draw: its kind (arrow | open | cross | none), where its tip
    // lies and the direction it points to.
    struct HeadPlacement {
        QString kind;
        QPointF tip;
        qreal angle = 0;
    };

    void drawHead(QPainter &painter, const Board &board, const LinkStyle &style, const HeadPlacement &placement) const;
};

// The family's own layout (arrangeSequence): participants on one row, in
// their current left-to-right order, spaced so that each message label fits
// between its lifelines; lanes compacted.
void arrange(const Board &board);
// The fragments' frames, headers and branch separators.
void drawFragments(QPainter &painter, const Board &board);
void drawFragmentLabels(QPainter &painter, const Board &board);

} // namespace diagram::sequence
