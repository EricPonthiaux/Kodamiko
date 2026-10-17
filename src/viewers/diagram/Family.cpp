#include "Family.h"

#include "Board.h"
#include "families/Families.h"

#include <QFileInfo>

namespace diagram {

const QList<const Family *> &allFamilies()
{
    // A family without shapes is not ported yet: it is left out.
    static const QList<const Family *> list = [] {
        QList<const Family *> all{
            &families::flowchart(), &families::sequence(), &families::classes(),  &families::states(),
            &families::dataModel(), &families::c4(),       &families::mindmap(),  &families::journey(),
            &families::productVision(), &families::explainer(),
        };
        QList<const Family *> ported;
        for (const Family *family : all) {
            if (!family->shapes.isEmpty())
                ported.append(family);
        }
        return ported;
    }();
    return list;
}

const Family *findFamily(const QString &id)
{
    for (const Family *family : allFamilies()) {
        if (family->id == id)
            return family;
    }
    return nullptr;
}

const QList<const Codec *> &allCodecs()
{
    // Detection order matters for PlantUML (no header keyword): class and
    // state have positive markers, sequence is the broadest so it goes last.
    static const QList<const Codec *> list = [] {
        QList<const Codec *> all;
        all += families::explainerCodecs();
        all += families::productVisionCodecs();
        all += families::dataModelCodecs();
        all += families::c4Codecs();
        all += families::journeyCodecs();
        all += families::flowchartCodecs();
        all += families::sequenceCodecs();
        all += families::classCodecs();
        all += families::stateCodecs();
        all += families::mindmapCodecs();
        QList<const Codec *> ordered;
        // Mermaid and the JSON scenes first, PlantUML after (class, state,
        // sequence).
        for (const Codec *codec : all) {
            if (codec->language() != QLatin1String("plantuml"))
                ordered.append(codec);
        }
        for (const QString &id : {QStringLiteral("plantuml-class"), QStringLiteral("plantuml-state"),
                                  QStringLiteral("plantuml-sequence")}) {
            for (const Codec *codec : all) {
                if (codec->id() == id)
                    ordered.append(codec);
            }
        }
        for (const Codec *codec : all) {
            if (codec->language() == QLatin1String("plantuml") && !ordered.contains(codec))
                ordered.append(codec);
        }
        return ordered;
    }();
    return list;
}

const Codec *findCodec(const QString &id)
{
    for (const Codec *codec : allCodecs()) {
        if (codec->id() == id)
            return codec;
    }
    return nullptr;
}

const Codec *codecFor(const QString &language, const QString &source)
{
    for (const Codec *codec : allCodecs()) {
        if (codec->language() == language && codec->matches(source))
            return codec;
    }
    return nullptr;
}

QString languageOfFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("mmd") || suffix == QLatin1String("mermaid"))
        return QStringLiteral("mermaid");
    if (suffix == QLatin1String("puml") || suffix == QLatin1String("plantuml") || suffix == QLatin1String("pu")
        || suffix == QLatin1String("iuml"))
        return QStringLiteral("plantuml");
    if (suffix == QLatin1String("board"))
        return QStringLiteral("board");
    if (suffix == QLatin1String("flow"))
        return QStringLiteral("flowboard");
    return {};
}

const Codec *defaultCodec(const Family &family)
{
    for (const QString &language : {QStringLiteral("mermaid"), QStringLiteral("board")}) {
        for (const Codec *codec : allCodecs()) {
            if (&codec->family() == &family && codec->language() == language)
                return codec;
        }
    }
    for (const Codec *codec : allCodecs()) {
        if (&codec->family() == &family)
            return codec;
    }
    return nullptr;
}

QJsonObject buildBoard(Board &board, const Codec &codec, const Model &model)
{
    if (board.family() != &codec.family())
        board.setFamily(&codec.family());
    else
        board.clear();
    QHash<QString, Shape *> byKey;
    int placed = 0;
    for (const Model::Node &node : model.nodes) {
        QJsonObject props = node.props;
        // Without a declared position, a provisional grid: what shows if the
        // layout fails, rather than a pile at (0, 0).
        if (!props.contains(QStringLiteral("x")) && !props.contains(QStringLiteral("y"))) {
            props.insert(QStringLiteral("x"), 40 + (placed % 4) * 240);
            props.insert(QStringLiteral("y"), 40 + (placed / 4) * 140);
            ++placed;
        }
        props.insert(QStringLiteral("key"), node.key);
        if (Shape *s = board.addShape(node.type, props))
            byKey.insert(node.key, s);
    }
    for (const Model::Edge &edge : model.links) {
        Shape *a = byKey.value(edge.from);
        Shape *b = byKey.value(edge.to);
        if (a && b)
            board.addLink(edge.type, a->id, b->id, edge.props);
    }
    board.meta = model.meta;
    board.resolveLabels();
    return model.meta;
}

} // namespace diagram
