// SPDX-License-Identifier: GPL-3.0-only

// The C4 family: its shapes and links, and the presets of the palette.

#include "C4.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QCoreApplication>

#include <memory>

namespace diagram::c4 {

// ---------------------------------------------------------------------------
// The family's presets

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("C4Diagram", text);
}

// An element preset: the element it creates, and how the menu names it.
struct ElementSpec {
    QString kind;
    QString variant = QStringLiteral("box");
    bool external = false;
    QString label;
    QString name;
    QString help;
};

ElementPreset elementPreset(const ElementSpec &spec)
{
    QStringList presetId{spec.kind};
    if (spec.variant != QLatin1String("box"))
        presetId.append(spec.variant);
    if (spec.external)
        presetId.append(QStringLiteral("external"));
    return ElementPreset{presetId.join(QLatin1Char('-')), spec.label, spec.help, QStringLiteral("c4"),
                         QJsonObject{{QStringLiteral("name"), spec.name},
                                     {QStringLiteral("kind"), spec.kind},
                                     {QStringLiteral("description"), QString()},
                                     {QStringLiteral("technology"), QString()},
                                     {QStringLiteral("external"), spec.external},
                                     {QStringLiteral("variant"), spec.variant}}};
}

struct Structural {
    const char *kind;
    QString noun;
    QString abbreviation;
};

// What an element of this silhouette is called: "Database", or the kind's own
// noun for a plain box.
QString variantNoun(const QString &variant, const QString &kindNoun)
{
    if (variant == QLatin1String("database"))
        return tr("Database");
    if (variant == QLatin1String("queue"))
        return tr("Message queue");
    return kindNoun;
}

QString variantHelpSuffix(const QString &variant)
{
    if (variant == QLatin1String("database"))
        return QStringLiteral(" · DB");
    if (variant == QLatin1String("queue"))
        return QStringLiteral(" · QUEUE");
    return {};
}

ElementPreset structuralPreset(const Structural &structural, const QString &variant, bool external)
{
    const QString base = variantNoun(variant, structural.noun);
    const QString name = external ? tr("External %1").arg(base.toLower()) : base;
    // The menu tells the kinds apart ("Container database"); the element is
    // named by what it is ("Database").
    QString label =
        variant == QLatin1String("box") ? structural.noun : structural.noun + QLatin1Char(' ') + base.toLower();
    if (external)
        label = tr("External %1").arg(label.toLower());
    const QString help = structural.abbreviation + variantHelpSuffix(variant);
    return elementPreset({.kind = QString::fromLatin1(structural.kind),
                          .variant = variant,
                          .external = external,
                          .label = label,
                          .name = name,
                          .help = help});
}

// People, then each structural kind as a box, a database and a queue,
// internal and external; then the boundaries.
QList<ElementPreset> elementPresets()
{
    QList<ElementPreset> presets{
        elementPreset(
            {.kind = QStringLiteral("person"), .label = tr("Person"), .name = tr("Person"), .help = tr("Person")}),
        elementPreset({.kind = QStringLiteral("person"),
                       .external = true,
                       .label = tr("External person"),
                       .name = tr("External person"),
                       .help = tr("Person")}),
    };
    const std::array<Structural, 3> structurals{{{"system", tr("System"), QStringLiteral("SYS")},
                                                 {"container", tr("Container"), QStringLiteral("CTR")},
                                                 {"component", tr("Component"), QStringLiteral("CMP")}}};
    for (const Structural &structural : structurals) {
        for (const char *variant : {"box", "database", "queue"}) {
            for (const bool external : {false, true})
                presets.append(structuralPreset(structural, QString::fromLatin1(variant), external));
        }
    }
    return presets;
}

ElementPreset boundaryPreset(const char *presetId, const QString &label, const char *kind)
{
    return ElementPreset{
        QString::fromLatin1(presetId),
        label,
        {},
        QStringLiteral("c4boundary"),
        QJsonObject{{QStringLiteral("name"), label}, {QStringLiteral("boundaryKind"), QString::fromLatin1(kind)}}};
}

QList<ElementPreset> boundaryPresets()
{
    return {boundaryPreset("enterprise-boundary", tr("Enterprise boundary"), "Enterprise_Boundary"),
            boundaryPreset("system-boundary", tr("System boundary"), "System_Boundary"),
            boundaryPreset("container-boundary", tr("Container boundary"), "Container_Boundary"),
            boundaryPreset("deployment-boundary", tr("Deployment node"), "Deployment_Node")};
}

enum class Direction {
    OneWay,
    BothWays
};

RelationPreset relationPreset(const char *presetId, const QString &label, const QString &edgeLabel, Direction direction)
{
    QJsonObject props{{QStringLiteral("label"), edgeLabel}};
    const bool both = direction == Direction::BothWays;
    props.insert(both ? QStringLiteral("bidirectional") : QStringLiteral("directed"), true);
    return RelationPreset{QString::fromLatin1(presetId), label, {}, props};
}

QList<RelationPreset> relationPresets()
{
    return {relationPreset("uses", tr("Uses"), tr("Uses"), Direction::OneWay),
            relationPreset("calls", tr("Calls"), tr("Calls"), Direction::OneWay),
            relationPreset("sends", tr("Sends"), tr("Sends"), Direction::OneWay),
            relationPreset("reads", tr("Reads"), tr("Reads"), Direction::OneWay),
            relationPreset("writes", tr("Writes"), tr("Writes"), Direction::OneWay),
            relationPreset("publishes", tr("Publishes"), tr("Publishes"), Direction::OneWay),
            relationPreset("consumes", tr("Consumes"), tr("Consumes"), Direction::OneWay),
            relationPreset("hosts", tr("Hosts"), tr("Hosts"), Direction::OneWay),
            relationPreset("depends", tr("Depends on"), tr("Depends on"), Direction::OneWay),
            relationPreset("exchanges", tr("Bidirectional exchange"), tr("Exchanges with"), Direction::BothWays)};
}

Family makeFamily()
{
    Family f;
    f.id = QStringLiteral("c4");
    f.title = tr("C4 architecture");
    f.shapes = {{QStringLiteral("c4boundary"), [] { return std::unique_ptr<Shape>(new C4BoundaryShape); }},
                {QStringLiteral("c4"), [] { return std::unique_ptr<Shape>(new C4ElementShape); }}};
    f.links = {{QStringLiteral("c4relation"), [] { return std::unique_ptr<Link>(new C4RelationLink); }, false, true}};
    f.defaultShape = QStringLiteral("c4");
    f.defaultLink = QStringLiteral("c4relation");
    f.graphvizClusters = true;
    f.graphvizSplines = QStringLiteral("spline");
    f.graphvizAdaptiveDirection = true;
    f.nestedCreation = true;
    f.elements = elementPresets() + boundaryPresets();
    f.relations = relationPresets();
    f.technologies = {
        QStringLiteral("HTTP/HTTPS"), QStringLiteral("REST/JSON"), QStringLiteral("WebSocket"), QStringLiteral("gRPC"),
        QStringLiteral("SQL"),        QStringLiteral("AMQP"),      QStringLiteral("Kafka"),     tr("Event")};
    return f;
}

} // namespace

} // namespace diagram::c4

// ---------------------------------------------------------------------------
// The family

namespace diagram::families {

const Family &c4()
{
    static const Family family = c4::makeFamily();
    return family;
}

} // namespace diagram::families
