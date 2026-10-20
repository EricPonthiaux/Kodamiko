// SPDX-License-Identifier: GPL-3.0-only

// Explainer boards as a JSON scene (sidecar's fence/sciilo-board.js and its
// contract, sciilo-board-v1.schema.json): a compact, versioned scene, easy
// for an LLM to produce and safe to render. Unlike a Mermaid diagram, its
// coordinates are source data: an element moved by an agent must not snap
// back to an older layout. The format is sidecar's, so that boards move
// between the two apps.

#include "Explainer.h"
#include "SceneReader.h"
#include "SceneWriter.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QRegularExpression>

#include <optional>

namespace diagram::explainer {

namespace {

class ExplainerBoard : public Codec {
public:
    QString id() const override { return QStringLiteral("explainer-board"); }

    QString language() const override { return QStringLiteral("board"); }

    QString title() const override { return QStringLiteral("Explainer board"); }

    const Family &family() const override { return families::explainer(); }

    // The scene carries its coordinates: they are source data, never laid
    // out again (only elements written without a place are).
    bool arrangeWithGraphviz() const override { return false; }

    bool sourceOwnsLayout() const override { return true; }

    bool matches(const QString &source) const override
    {
        static const QRegularExpression pattern(QStringLiteral("^\\s*\\{[\\s\\S]*\"format\"\\s*:\\s*\"sciilo-board\""));
        return pattern.match(source).hasMatch();
    }

    bool parse(const QString &source, Model &model, QString *error) const override
    {
        const std::optional<QString> refusal = readScene(source, model);
        if (refusal && error)
            *error = *refusal;
        return !refusal;
    }

    QString serialize(const Board &board, const QJsonObject &meta) const override { return writeScene(board, meta); }

    QString blank() const override
    {
        return QStringLiteral("{\n  \"format\": \"sciilo-board\",\n  \"version\": 1,\n  \"canvas\": {\n"
                              "    \"width\": 1200,\n    \"height\": 800\n  },\n  \"elements\": []\n}");
    }
};

} // namespace

} // namespace diagram::explainer

namespace diagram::families {

QList<const Codec *> explainerCodecs()
{
    static const explainer::ExplainerBoard board;
    return {&board};
}

} // namespace diagram::families
