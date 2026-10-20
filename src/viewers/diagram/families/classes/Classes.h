// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "viewers/diagram/Shape.h"

// UML class diagrams, ported from sidecar's class-diagram.js: a classifier
// box (name, «stereotype», attributes, methods) that follows its longest
// member, and the UML relation with its marker at the `to` end and a
// cardinality near each end.
namespace diagram::classes {

// The default label of a relation kind ("extension" → "Inheritance"), in
// English (sidecar's DIAGRAM_TERMS.en); "Relationship" for an unknown kind.
QString defaultRelationLabel(const QString &kind);

class ClassShape : public Shape {
public:
    ClassShape();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    void onDoubleClick(Board &board) override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;

    QString displayText() const override { return name; }

    QList<Property> properties() const override;
    QVariant propertyValue(const QString &propertyId) const override;
    void setPropertyValue(const QString &propertyId, const QVariant &value) override;

    // The text edited in place: «stereotype» and name, then `--` before the
    // attributes and before the methods.
    QString editText() const;
    void setEditText(const QString &value);

    QString name;
    QString stereotype; // "interface", "abstract"…; empty for a plain class
    QStringList attributes;
    QStringList methods;
};

class ClassLink : public Link {
public:
    ClassLink();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    QStringList graphvizAttributes(const Board &board) const override;
    void draw(QPainter &painter, const Board &board, bool selected) const override;
    QList<Property> properties() const override;
    QVariant propertyValue(const QString &propertyId) const override;
    void setPropertyValue(const QString &propertyId, const QVariant &value) override;

    // link | association | extension | realization | composition |
    // aggregation | dependency | dotted
    QString kind = QStringLiteral("association");
    QString fromCard; // near the tail
    QString toCard;   // near the marker end

private:
    void drawMarker(QPainter &painter, const Board &board, const Route &route, const LinkStyle &style) const;
    void drawCards(QPainter &painter, const Board &board, const Route &route, qreal angle) const;
};

} // namespace diagram::classes
