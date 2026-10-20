// SPDX-License-Identifier: GPL-3.0-only

// Product visions (sidecar's fence/sciilo-product-vision.js). A product
// vision stays ordinary Mermaid mindmap source for Claude: a marker comment
// and stable node keys let Duet show that tree as the product vision's cards
// instead of a generic mind map.

#include "ProductVision.h"
#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/SourceText.h"
#include "viewers/diagram/families/Families.h"
#include "viewers/diagram/families/mindmap/Mindmap.h"

#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

namespace diagram::productvision {

namespace {

using namespace diagram::source;

// Written by Duet; sidecar writes `%% sciilo:product-vision`, read alike.
const QString marker = QStringLiteral("%% product-vision");

bool isProductVisionSource(const QString &source)
{
    // On the raw source: the Mermaid line reader drops `%%` comments.
    static const QRegularExpression markerPattern(QStringLiteral("%%\\s*(?:sciilo:)?product-vision\\b"),
                                                  QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression mindmapPattern(QStringLiteral("(?:^|\\n)\\s*mindmap\\b"),
                                                   QRegularExpression::CaseInsensitiveOption);
    return markerPattern.match(source).hasMatch() && mindmapPattern.match(source).hasMatch();
}

// The texts under `key`, depth first.
QStringList descendants(const QString &key, const Model &tree)
{
    QHash<QString, QStringList> children;
    for (const Model::Edge &link : tree.links)
        children[link.from].append(link.to);
    QHash<QString, QString> texts;
    for (const Model::Node &node : tree.nodes)
        texts.insert(node.key, node.props.value(QStringLiteral("text")).toString());
    QStringList result;
    QSet<QString> visited;
    const std::function<void(const QString &)> visit = [&](const QString &current) {
        if (visited.contains(current))
            return;
        visited.insert(current);
        for (const QString &child : children.value(current)) {
            if (!texts.contains(child))
                continue;
            const QString text = texts.value(child).trimmed();
            if (!text.isEmpty())
                result.append(text);
            visit(child);
        }
    };
    visit(key);
    return result;
}

// The product's node: the one keyed `product`, else the first root of the tree.
const Model::Node *findProduct(const Model &tree, const QHash<QString, const Model::Node *> &byKey)
{
    if (const Model::Node *product = byKey.value(QStringLiteral("product")))
        return product;
    QSet<QString> incoming;
    for (const Model::Edge &link : tree.links)
        incoming.insert(link.to);
    const auto isRoot = [&incoming](const Model::Node &node) { return !incoming.contains(node.key); };
    const auto root = std::ranges::find_if(tree.nodes, isRoot);
    return root == tree.nodes.end() ? nullptr : &*root;
}

// One hierarchy, both shown and laid out by Graphviz: every arrow joins
// adjacent levels, so no long edge runs behind another card.
void appendArrows(Model &model)
{
    QSet<QString> visible;
    for (const Model::Node &node : model.nodes)
        visible.insert(node.key);
    static constexpr std::array<std::pair<const char *, const char *>, 7> arrows{{
        {"product", "emotions"},
        {"emotions", "problem"},
        {"emotions", "users"},
        {"emotions", "value"},
        {"problem", "capabilities"},
        {"users", "journeys"},
        {"value", "unknowns"},
    }};
    for (const auto &[fromText, toText] : arrows) {
        const QString from = QString::fromLatin1(fromText);
        const QString to = QString::fromLatin1(toText);
        if (visible.contains(from) && visible.contains(to))
            model.links.append({QStringLiteral("productVisionLink"), from, to, QJsonObject{}});
    }
}

class MermaidProductVision : public Codec {
public:
    QString id() const override { return QStringLiteral("mermaid-product-vision"); }

    QString language() const override { return QStringLiteral("mermaid"); }

    QString title() const override { return QStringLiteral("Product vision"); }

    const Family &family() const override { return families::productVision(); }

    bool matches(const QString &source) const override { return isProductVisionSource(source); }

    bool parse(const QString &source, Model &model, QString *) const override
    {
        Model tree;
        mindmap::parseTree(source, tree);
        QHash<QString, const Model::Node *> byKey;
        for (const Model::Node &node : tree.nodes)
            byKey.insert(node.key, &node);
        const Model::Node *product = findProduct(tree, byKey);
        const QString productTitle = product ? product->props.value(QStringLiteral("text")).toString() : QString();
        const QStringList vision =
            byKey.contains(QStringLiteral("vision")) ? descendants(QStringLiteral("vision"), tree) : QStringList();
        model.nodes.append({QStringLiteral("productVision"), QStringLiteral("product"),
                            QJsonObject{{QStringLiteral("role"), QStringLiteral("product")},
                                        {QStringLiteral("title"),
                                         productTitle.isEmpty() ? term(QStringLiteral("product")) : productTitle},
                                        {QStringLiteral("items"), toJsonArray(vision)}}});
        for (const QString &role : sections()) {
            const QStringList items = byKey.contains(role) ? descendants(role, tree) : QStringList();
            // The stable Mermaid branch stays in the source for later patches,
            // but an empty Unknowns card means nothing in a completed vision:
            // it must not look like another clarification is due.
            if (role == QLatin1String("unknowns") && items.isEmpty())
                continue;
            model.nodes.append({QStringLiteral("productVision"), role,
                                QJsonObject{{QStringLiteral("role"), role},
                                            {QStringLiteral("title"), term(role)},
                                            {QStringLiteral("items"), toJsonArray(items)}}});
        }
        appendArrows(model);
        model.meta = QJsonObject{{QStringLiteral("purpose"), QStringLiteral("product-vision")},
                                 {QStringLiteral("rankdir"), QStringLiteral("TB")},
                                 {QStringLiteral("engine"), QStringLiteral("dot")}};
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &) const override
    {
        const auto cardOf = [&board](const QString &role) -> const ProductVisionShape * {
            for (const auto &s : board.shapes()) {
                const auto *card = dynamic_cast<const ProductVisionShape *>(s.get());
                if (card && card->role == role)
                    return card;
            }
            return nullptr;
        };
        const ProductVisionShape *root = cardOf(QStringLiteral("product"));
        QStringList out{
            marker, QStringLiteral("mindmap"),
            QStringLiteral("  product((%1))")
                .arg(mermaidQuote(root && !root->title.isEmpty() ? root->title : term(QStringLiteral("product"))))};
        const auto emitBranch = [&out](const QString &role, const QStringList &items) {
            out.append(QStringLiteral("    %1[%2]").arg(role, mermaidQuote(term(role))));
            int index = 0;
            for (const QString &item : items) {
                if (!item.trimmed().isEmpty())
                    out.append(
                        QStringLiteral("      %1_%2[%3]").arg(role).arg(++index).arg(mermaidQuote(item.trimmed())));
            }
        };
        emitBranch(QStringLiteral("vision"), root ? root->items : QStringList());
        for (const QString &role : sections()) {
            const ProductVisionShape *card = cardOf(role);
            emitBranch(role, card ? card->items : QStringList());
        }
        return out.join(QLatin1Char('\n'));
    }

    QString blank() const override
    {
        return QStringLiteral("%% product-vision\n"
                              "mindmap\n"
                              "  product((\"My product\"))\n"
                              "    vision[\"Vision\"]\n"
                              "      vision_1[\"The human change this product aims to make possible\"]\n"
                              "    emotions[\"Desired momentum\"]\n"
                              "      emotions_1[\"I understand the direction and want to take action\"]\n"
                              "    problem[\"Problem\"]\n"
                              "      problem_1[\"The need to solve\"]\n"
                              "    users[\"Users\"]\n"
                              "      users_1[\"Primary user\"]\n"
                              "    value[\"Value proposition\"]\n"
                              "      value_1[\"The essential benefit\"]\n"
                              "    capabilities[\"Main capabilities\"]\n"
                              "      capabilities_1[\"A key capability\"]\n"
                              "    journeys[\"Key journeys\"]\n"
                              "      journeys_1[\"The main journey\"]\n"
                              "    unknowns[\"Unknowns to confirm\"]\n"
                              "      unknowns_1[\"An assumption to verify\"]");
    }
};

} // namespace

} // namespace diagram::productvision

namespace diagram::families {

QList<const Codec *> productVisionCodecs()
{
    static const productvision::MermaidProductVision mermaid;
    return {&mermaid};
}

} // namespace diagram::families
