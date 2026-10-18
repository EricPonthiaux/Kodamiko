#include "EffectiveSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>
#include <optional>
#include <utility>

using ClaudeConfig::Scope;

namespace EffectiveSettings {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("EffectiveSettings", text);
}

// A merged value and the layer it comes from.
struct Node {
    enum class Kind {
        Scalar,
        Object,
        Array
    };
    Kind kind = Kind::Scalar;
    Scope scope = Scope::User;
    QJsonValue scalar;
    QList<std::pair<QString, Node>> members; // Object
    QList<Node> items;                       // Array
    // The values this one replaced, the latest first.
    QList<std::pair<Scope, QJsonValue>> replaced;
};

Node nodeFrom(const QJsonValue &value, Scope scope)
{
    Node node;
    node.scope = scope;
    if (value.isObject()) {
        node.kind = Node::Kind::Object;
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            node.members.append({it.key(), nodeFrom(it.value(), scope)});
    } else if (value.isArray()) {
        node.kind = Node::Kind::Array;
        for (const QJsonValue &item : value.toArray())
            node.items.append(nodeFrom(item, scope));
    } else {
        node.scalar = value;
    }
    return node;
}

QJsonValue toJson(const Node &node)
{
    switch (node.kind) {
    case Node::Kind::Object: {
        QJsonObject object;
        for (const auto &[key, member] : node.members)
            object.insert(key, toJson(member));
        return object;
    }
    case Node::Kind::Array: {
        QJsonArray array;
        for (const Node &item : node.items)
            array.append(toJson(item));
        return array;
    }
    case Node::Kind::Scalar:
        break;
    }
    return node.scalar;
}

void mergeInto(Node &node, const QJsonValue &value, Scope scope)
{
    if (node.kind == Node::Kind::Object && value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            const auto member = std::find_if(node.members.begin(), node.members.end(),
                                             [&](const auto &pair) { return pair.first == it.key(); });
            if (member == node.members.end())
                node.members.append({it.key(), nodeFrom(it.value(), scope)});
            else
                mergeInto(member->second, it.value(), scope);
        }
        return;
    }
    if (node.kind == Node::Kind::Array && value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            const bool present = std::any_of(node.items.cbegin(), node.items.cend(),
                                             [&](const Node &existing) { return toJson(existing) == item; });
            if (!present)
                node.items.append(nodeFrom(item, scope));
        }
        return;
    }
    QList<std::pair<Scope, QJsonValue>> replaced = node.replaced;
    replaced.prepend({node.scope, toJson(node)});
    node = nodeFrom(value, scope);
    node.replaced = std::move(replaced);
}

Node mergeLayers(const QList<Layer> &layers)
{
    Node root;
    root.kind = Node::Kind::Object;
    for (const Layer &layer : layers) {
        QJsonObject settings = layer.settings;
        settings.remove(QStringLiteral("$schema")); // for the editor, not a setting
        mergeInto(root, settings, layer.scope);
    }
    return root;
}

QString compact(const QJsonValue &value)
{
    // QJsonDocument writes only objects and arrays.
    const QByteArray json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}

QString scopeComment(const Node &node)
{
    QString comment = ClaudeConfig::scopeTitle(node.scope);
    QStringList replaced;
    for (const auto &[scope, value] : node.replaced) {
        QString shown = compact(value);
        if (shown.size() > 40)
            shown = shown.left(39) + QChar(0x2026);
        replaced.append(QStringLiteral("%1: %2").arg(ClaudeConfig::scopeTitle(scope), shown));
    }
    if (!replaced.isEmpty())
        comment += tr(", over ") + replaced.join(QStringLiteral(", "));
    return comment;
}

struct Line {
    QString code;
    QString comment;
};

// Lines of `node`; its scope is written only where it differs from the
// scope of the line above it in the tree (`shown`), or where it replaced
// something.
void renderNode(const Node &node,
                const QString &indent,
                const QString &prefix,
                bool comma,
                std::optional<Scope> shown,
                QList<Line> *lines)
{
    const QString end = comma ? QStringLiteral(",") : QString();
    const bool annotate = !shown || *shown != node.scope || !node.replaced.isEmpty();
    const QString comment = annotate ? scopeComment(node) : QString();
    const std::optional<Scope> childShown = annotate ? std::optional(node.scope) : shown;

    if (node.kind == Node::Kind::Scalar || (node.kind == Node::Kind::Object && node.members.isEmpty()) ||
        (node.kind == Node::Kind::Array && node.items.isEmpty())) {
        lines->append({indent + prefix + compact(toJson(node)) + end, comment});
        return;
    }
    const QString inner = indent + QStringLiteral("  ");
    if (node.kind == Node::Kind::Object) {
        lines->append({indent + prefix + QLatin1Char('{'), comment});
        for (qsizetype i = 0; i < node.members.size(); ++i) {
            const auto &[key, member] = node.members[i];
            renderNode(member, inner, compact(key) + QStringLiteral(": "), i + 1 < node.members.size(), childShown,
                       lines);
        }
        lines->append({indent + QLatin1Char('}') + end, {}});
    } else {
        lines->append({indent + prefix + QLatin1Char('['), comment});
        for (qsizetype i = 0; i < node.items.size(); ++i)
            renderNode(node.items[i], inner, {}, i + 1 < node.items.size(), childShown, lines);
        lines->append({indent + QLatin1Char(']') + end, {}});
    }
}

// "~/.claude/settings.json", ".claude/settings.json" (in the project).
QString shownPath(const QString &path, const QString &projectDir)
{
    const QString inProject = QDir(projectDir).relativeFilePath(path);
    if (!inProject.startsWith(QLatin1String("..")))
        return inProject;
    const QString home = QDir::homePath() + QLatin1Char('/');
    return path.startsWith(home) ? QStringLiteral("~/") + path.mid(home.size()) : QDir::toNativeSeparators(path);
}

} // namespace

QList<Layer> read(const QString &projectDir, const QString &sessionSettings)
{
    QList<Layer> layers;
    for (const ClaudeConfig::SettingsFile &file : ClaudeConfig::settingsFiles(projectDir, sessionSettings)) {
        Layer layer;
        layer.scope = file.scope;
        layer.path = file.path;
        QFile input(file.path);
        layer.exists = input.open(QIODevice::ReadOnly);
        if (layer.exists) {
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(input.readAll(), &error);
            if (error.error != QJsonParseError::NoError)
                layer.error = tr("%1 at character %2").arg(error.errorString()).arg(error.offset);
            else if (!document.isObject())
                layer.error = tr("not a JSON object");
            else
                layer.settings = document.object();
        }
        layers.append(layer);
    }
    return layers;
}

QJsonObject merged(const QList<Layer> &layers)
{
    return toJson(mergeLayers(layers)).toObject();
}

QString render(const QList<Layer> &layers, const QString &projectDir)
{
    QStringList header{
        tr("// Effective Claude Code settings for %1").arg(QDir::toNativeSeparators(projectDir)),
        tr("// What Claude applies once its settings files are merged, each value followed"),
        tr("// by the file it comes from. Objects merge key by key, arrays (permissions,"),
        tr("// hooks...) are put together, any other value is replaced by the later file's."),
        tr("// Read-only; edit the files themselves. From the lowest precedence to the highest:"),
    };
    int scopeWidth = 0;
    for (const Layer &layer : layers)
        scopeWidth = std::max(scopeWidth, int(ClaudeConfig::scopeTitle(layer.scope).size()));
    for (const Layer &layer : layers) {
        QString line = QStringLiteral("//   %1  %2")
                           .arg(ClaudeConfig::scopeTitle(layer.scope).leftJustified(scopeWidth),
                                shownPath(layer.path, projectDir));
        if (!layer.exists)
            line += tr("  (none)");
        else if (!layer.error.isEmpty())
            line += tr("  (ignored: %1)").arg(layer.error);
        header.append(line);
    }

    // The root belongs to no file: each of its keys says where it comes from.
    const Node root = mergeLayers(layers);
    QList<Line> lines{{QStringLiteral("{"), {}}};
    for (qsizetype i = 0; i < root.members.size(); ++i) {
        const auto &[key, member] = root.members[i];
        renderNode(member, QStringLiteral("  "), compact(key) + QStringLiteral(": "), i + 1 < root.members.size(),
                   std::nullopt, &lines);
    }
    lines.append({QStringLiteral("}"), {}});
    // Comments aligned, unless a line is too long for it.
    int width = 0;
    for (const Line &line : std::as_const(lines)) {
        if (!line.comment.isEmpty() && line.code.size() <= 60)
            width = std::max(width, int(line.code.size()));
    }
    QStringList body;
    for (const Line &line : std::as_const(lines)) {
        body.append(line.comment.isEmpty()
                        ? line.code
                        : QStringLiteral("%1  // %2").arg(line.code.leftJustified(width), line.comment));
    }
    return header.join(QLatin1Char('\n')) + QStringLiteral("\n\n") + body.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace EffectiveSettings
