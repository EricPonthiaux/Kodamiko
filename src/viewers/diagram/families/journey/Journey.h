// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "viewers/diagram/Shape.h"

// User journeys, ported from sidecar's journey-diagram.js: task cards carry
// their section, their score (1 to 5, the mood of the step) and their
// actors; ordinary links keep the order of the steps.
namespace diagram::journey {

class JourneyTaskShape : public Shape {
public:
    JourneyTaskShape();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    void onDoubleClick(Board &board) override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;

    QString displayText() const override { return text; }

    QList<Property> properties() const override;
    QVariant propertyValue(const QString &propertyId) const override;
    void setPropertyValue(const QString &propertyId, const QVariant &value) override;

    struct TextLayout {
        QStringList header; // the section and the score, as dots
        QStringList title;
        QStringList actors;
    };

    TextLayout textLayout(qreal maxWidth) const;

    QString text;
    QString section;
    int score = 3;
    QStringList actors;
};

class JourneyLink : public Link {
public:
    JourneyLink();
};

} // namespace diagram::journey
