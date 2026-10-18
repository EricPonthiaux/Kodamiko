#pragma once

#include "viewers/diagram/Shape.h"

// Product visions, ported from sidecar's product-vision-diagram.js: a fixed
// skeleton of cards (the product and its vision, the desired momentum, the
// problem, the users, the value, the capabilities, the journeys, the
// unknowns), each a titled list. The source stays an ordinary Mermaid mind
// map, marked as a product vision, so that Claude reads and patches it like
// any other.
namespace diagram::productvision {

// The sections under the product, in reading order.
const QStringList &sections();
// The English name of a role ("problem" → "Problem"), "vision", "product"
// and "empty" (the placeholder of an empty card) included.
QString term(const QString &role);

class ProductVisionShape : public Shape {
public:
    ProductVisionShape();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    void resizeTo(qreal width, qreal height, const Board &board) override;
    void onDoubleClick(Board &board) override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;
    QString displayText() const override;

    QString role = QStringLiteral("problem");
    QString title;
    QStringList items;

private:
    bool isRoot() const { return role == QLatin1String("product"); }

    // The momentum card stands out under the product.
    bool isFeatured() const { return role == QLatin1String("emotions"); }

    // Lays the text out for the current width; returns the height it needs.
    qreal layoutText();

    QStringList titleLines_;
    QList<QStringList> itemLines_;
    qreal itemLineHeight_ = 20;
};

class ProductVisionLink : public Link {
public:
    ProductVisionLink();

    LinkStyle drawStyle(const Board &board, bool selected) const override;
    void draw(QPainter &painter, const Board &board, bool selected) const override;

private:
    // The arrow from the product to its momentum, the spine of the vision.
    bool isCentral(const Board &board) const;
};

} // namespace diagram::productvision
