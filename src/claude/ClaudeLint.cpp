#include "ClaudeLint.h"

#include "ClaudeConfig.h"
#include "diagnostics/JsonSchema.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include <functional>

namespace ClaudeLint {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("ClaudeLint", text);
}

const JsonSchema &settingsSchema()
{
    static const JsonSchema schema = [] {
        QFile file(QStringLiteral(":/schemas/claude-code-settings.json"));
        if (!file.open(QIODevice::ReadOnly))
            return JsonSchema({}); // checks nothing
        return JsonSchema(QJsonDocument::fromJson(file.readAll()).object());
    }();
    return schema;
}

bool isClaudeFolder(const QDir &dir)
{
    return dir.dirName() == QLatin1String(".claude")
           || QDir::cleanPath(dir.absolutePath()) == QDir::cleanPath(QFileInfo(ClaudeConfig::configDir()).absoluteFilePath());
}

Diagnostics checkSettings(QByteArrayView text)
{
    const JsonParseResult parsed = parseJson(text);
    if (!parsed.root) {
        // At the character that is wrong; at the end, on the last one.
        const int start = std::max(0, std::min(parsed.errorOffset, int(text.size()) - 1));
        return {{Diagnostic::Severity::Error, start, start + 1, parsed.error}};
    }
    if (parsed.root->type != QJsonValue::Object)
        return {{Diagnostic::Severity::Error, parsed.root->start, parsed.root->start + 1,
                 tr("Settings are a JSON object: { … }")}};
    return settingsSchema().validate(*parsed.root);
}

struct Field {
    QString value;
    int lineStart = 0; // the line's byte offsets
    int lineEnd = 0;
    int valueStart = 0;
};

Diagnostics checkFrontMatter(Kind kind, QByteArrayView text)
{
    const auto warning = [](int start, int end, const QString &message) {
        return Diagnostic{Diagnostic::Severity::Warning, start, std::max(end, start + 1), message};
    };
    // Lines as byte ranges, line breaks left out.
    QList<std::pair<int, int>> lines;
    for (int start = 0; start <= text.size();) {
        int end = int(text.indexOf('\n', start));
        if (end < 0)
            end = int(text.size());
        int contentEnd = end;
        if (contentEnd > start && text[contentEnd - 1] == '\r')
            --contentEnd;
        lines.append({start, contentEnd});
        start = end + 1;
    }
    const auto lineText = [&](qsizetype i) {
        return QString::fromUtf8(text.sliced(lines[i].first, lines[i].second - lines[i].first));
    };

    const QString what = kind == Kind::Skill ? tr("skill") : tr("agent");
    if (lines.isEmpty() || lineText(0).trimmed() != QLatin1String("---"))
        return {warning(0, lines.isEmpty() ? 0 : lines[0].second,
                        tr("No front matter: Claude knows a %1 by the name and description between two “---” "
                           "lines at the top").arg(what))};
    QHash<QString, Field> fields;
    qsizetype closing = -1;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QString line = lineText(i);
        if (line.trimmed() == QLatin1String("---")) {
            closing = i;
            break;
        }
        const qsizetype colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0 || line.front().isSpace())
            continue;
        Field field;
        field.lineStart = lines[i].first;
        field.lineEnd = lines[i].second;
        const QString rest = line.mid(colon + 1);
        field.value = rest.trimmed();
        if (field.value.size() >= 2 && (field.value.front() == QLatin1Char('"') || field.value.front() == QLatin1Char('\''))
            && field.value.back() == field.value.front())
            field.value = field.value.mid(1, field.value.size() - 2);
        qsizetype leading = 0;
        while (leading < rest.size() && rest[leading].isSpace())
            ++leading;
        field.valueStart = field.lineStart + int(line.left(colon + 1 + leading).toUtf8().size());
        fields.insert(line.left(colon).trimmed(), field);
    }
    if (closing < 0)
        return {{Diagnostic::Severity::Error, lines[0].first, lines[0].second,
                 tr("The front matter has no closing “---” line")}};

    Diagnostics diagnostics;
    const std::pair<int, int> opening = lines[0];
    if (const auto name = fields.constFind(QStringLiteral("name")); name != fields.cend()) {
        if (!ClaudeConfig::isValidName(name->value))
            diagnostics.append(warning(name->valueStart, name->lineEnd,
                                       tr("A name is lowercase letters, digits and hyphens, at most 64 characters")));
    } else if (kind == Kind::Agent) {
        diagnostics.append(warning(opening.first, opening.second, tr("Missing “name”: how Claude calls this agent")));
    }
    const auto description = fields.constFind(QStringLiteral("description"));
    if (description == fields.cend() || description->value.isEmpty()) {
        const QString message = kind == Kind::Skill
                                    ? tr("Missing “description”: Claude reads it to decide when to use the skill")
                                    : tr("Missing “description”: Claude reads it to decide when to hand a task to the agent");
        diagnostics.append(description == fields.cend() ? warning(opening.first, opening.second, message)
                                                        : warning(description->lineStart, description->lineEnd, message));
    } else if (kind == Kind::Skill && description->value.size() > 1024) {
        diagnostics.append(warning(description->valueStart, description->lineEnd,
                                   tr("At most 1024 characters: Claude may cut a longer description")));
    }
    return diagnostics;
}

} // namespace

Kind kindOf(const QString &path)
{
    const QFileInfo info(path);
    const QString name = info.fileName();
    const QDir dir = info.dir();
    if (name == QLatin1String("managed-settings.json"))
        return Kind::Settings;
    if ((name == QLatin1String("settings.json") || name == QLatin1String("settings.local.json")) && isClaudeFolder(dir))
        return Kind::Settings;
    if (name == QLatin1String("SKILL.md") && QFileInfo(dir.absolutePath()).dir().dirName() == QLatin1String("skills"))
        return Kind::Skill;
    if (info.suffix() == QLatin1String("md") && dir.dirName() == QLatin1String("agents")
        && isClaudeFolder(QFileInfo(dir.absolutePath()).dir()))
        return Kind::Agent;
    return Kind::None;
}

Diagnostics check(Kind kind, QByteArrayView text)
{
    switch (kind) {
    case Kind::Settings: return checkSettings(text);
    case Kind::Skill:
    case Kind::Agent: return checkFrontMatter(kind, text);
    case Kind::None: break;
    }
    return {};
}

QString settingsHelp(QByteArrayView text, int position)
{
    const JsonParseResult parsed = parseJson(text);
    if (!parsed.root)
        return {};
    QStringList path;
    const std::function<bool(const JsonNode &)> find = [&](const JsonNode &node) {
        for (const JsonMember &member : node.members) {
            path.append(member.key);
            if (position >= member.keyStart && position < member.keyEnd)
                return true;
            if (position >= member.value.start && position < member.value.end) {
                find(member.value); // a key inside, or this one
                return true;
            }
            path.removeLast();
        }
        for (qsizetype i = 0; i < node.items.size(); ++i) {
            path.append(QString::number(i));
            if (position >= node.items[i].start && position < node.items[i].end) {
                find(node.items[i]);
                return true;
            }
            path.removeLast();
        }
        return false;
    };
    if (!find(*parsed.root))
        return {};
    const QString description = settingsSchema().description(path);
    QString key = path.last();
    for (qsizetype i = path.size() - 1; i > 0 && !key.isEmpty() && key.front().isDigit(); --i)
        key = path[i - 1]; // an item: the list's name
    return description.isEmpty() ? QString() : QStringLiteral("%1\n%2").arg(key, description);
}

} // namespace ClaudeLint
