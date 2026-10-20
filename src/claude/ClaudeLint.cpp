// SPDX-License-Identifier: GPL-3.0-only

#include "ClaudeLint.h"

#include "ClaudeConfig.h"
#include "diagnostics/JsonSchema.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include <optional>
#include <utility>

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
    return dir.dirName() == QLatin1String(".claude") ||
           QDir::cleanPath(dir.absolutePath()) ==
               QDir::cleanPath(QFileInfo(ClaudeConfig::configDir()).absoluteFilePath());
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

// A line's byte offsets, line break left out.
using LineRange = std::pair<int, int>;

QList<LineRange> splitLines(QByteArrayView text)
{
    QList<LineRange> lines;
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
    return lines;
}

QString lineText(QByteArrayView text, LineRange line)
{
    return QString::fromUtf8(text.sliced(line.first, line.second - line.first));
}

Diagnostic warning(int start, int end, const QString &message)
{
    return Diagnostic{Diagnostic::Severity::Warning, start, std::max(end, start + 1), message};
}

QString unquoted(const QString &value)
{
    const bool isQuoted = value.size() >= 2 &&
                          (value.front() == QLatin1Char('"') || value.front() == QLatin1Char('\'')) &&
                          value.back() == value.front();
    return isQuoted ? value.mid(1, value.size() - 2) : value;
}

// "key: value" on `line`; nullopt for a line that is not a field (an
// indented continuation, a list item...).
std::optional<std::pair<QString, Field>> parseField(const QString &line, LineRange range)
{
    const qsizetype colon = line.indexOf(QLatin1Char(':'));
    if (colon <= 0 || line.front().isSpace())
        return std::nullopt;
    Field field;
    field.lineStart = range.first;
    field.lineEnd = range.second;
    const QString rest = line.mid(colon + 1);
    field.value = unquoted(rest.trimmed());
    qsizetype leading = 0;
    while (leading < rest.size() && rest[leading].isSpace())
        ++leading;
    field.valueStart = field.lineStart + int(line.left(colon + 1 + leading).toUtf8().size());
    return std::pair{line.left(colon).trimmed(), field};
}

// The fields between the opening line and the closing "---"; nullopt when
// there is no closing line.
std::optional<QHash<QString, Field>> readFields(QByteArrayView text, const QList<LineRange> &lines)
{
    QHash<QString, Field> fields;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QString line = lineText(text, lines[i]);
        if (line.trimmed() == QLatin1String("---"))
            return fields;
        if (const auto field = parseField(line, lines[i]))
            fields.insert(field->first, field->second);
    }
    return std::nullopt;
}

std::optional<Diagnostic> checkName(Kind kind, const QHash<QString, Field> &fields, LineRange opening)
{
    const auto name = fields.constFind(QStringLiteral("name"));
    if (name == fields.cend()) {
        if (kind == Kind::Agent)
            return warning(opening.first, opening.second, tr("Missing “name”: how Claude calls this agent"));
        return std::nullopt;
    }
    if (!ClaudeConfig::isValidName(name->value))
        return warning(name->valueStart, name->lineEnd,
                       tr("A name is lowercase letters, digits and hyphens, at most 64 characters"));
    return std::nullopt;
}

std::optional<Diagnostic> checkDescription(Kind kind, const QHash<QString, Field> &fields, LineRange opening)
{
    const auto description = fields.constFind(QStringLiteral("description"));
    if (description == fields.cend() || description->value.isEmpty()) {
        const QString message =
            kind == Kind::Skill
                ? tr("Missing “description”: Claude reads it to decide when to use the skill")
                : tr("Missing “description”: Claude reads it to decide when to hand a task to the agent");
        if (description == fields.cend())
            return warning(opening.first, opening.second, message);
        return warning(description->lineStart, description->lineEnd, message);
    }
    if (kind == Kind::Skill && description->value.size() > 1024)
        return warning(description->valueStart, description->lineEnd,
                       tr("At most 1024 characters: Claude may cut a longer description"));
    return std::nullopt;
}

Diagnostics checkFrontMatter(Kind kind, QByteArrayView text)
{
    const QList<LineRange> lines = splitLines(text);
    const QString what = kind == Kind::Skill ? tr("skill") : tr("agent");
    if (lines.isEmpty() || lineText(text, lines[0]).trimmed() != QLatin1String("---"))
        return {warning(0, lines.isEmpty() ? 0 : lines[0].second,
                        tr("No front matter: Claude knows a %1 by the name and description between two “---” "
                           "lines at the top")
                            .arg(what))};
    const std::optional<QHash<QString, Field>> fields = readFields(text, lines);
    if (!fields)
        return {{Diagnostic::Severity::Error, lines[0].first, lines[0].second,
                 tr("The front matter has no closing “---” line")}};

    Diagnostics diagnostics;
    for (const std::optional<Diagnostic> &found :
         {checkName(kind, *fields, lines[0]), checkDescription(kind, *fields, lines[0])}) {
        if (found)
            diagnostics.append(*found);
    }
    return diagnostics;
}

bool isWithin(int position, int start, int end)
{
    return position >= start && position < end;
}

// The keys and item indexes leading to the key or value at `position`.
std::optional<QStringList> pathAt(const JsonNode &node, int position)
{
    for (const JsonMember &member : node.members) {
        if (isWithin(position, member.keyStart, member.keyEnd))
            return QStringList{member.key};
        if (isWithin(position, member.value.start, member.value.end)) // a key inside, or this one
            return QStringList{member.key} + pathAt(member.value, position).value_or(QStringList());
    }
    for (qsizetype i = 0; i < node.items.size(); ++i) {
        if (isWithin(position, node.items[i].start, node.items[i].end))
            return QStringList{QString::number(i)} + pathAt(node.items[i], position).value_or(QStringList());
    }
    return std::nullopt;
}

// The last key of `path`; for an item, the name of its list.
QString keyNamed(const QStringList &path)
{
    QString key = path.last();
    for (qsizetype i = path.size() - 1; i > 0 && !key.isEmpty() && key.front().isDigit(); --i)
        key = path[i - 1];
    return key;
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
    if (info.suffix() == QLatin1String("md") && dir.dirName() == QLatin1String("agents") &&
        isClaudeFolder(QFileInfo(dir.absolutePath()).dir()))
        return Kind::Agent;
    return Kind::None;
}

Diagnostics check(Kind kind, QByteArrayView text)
{
    switch (kind) {
    case Kind::Settings:
        return checkSettings(text);
    case Kind::Skill:
    case Kind::Agent:
        return checkFrontMatter(kind, text);
    case Kind::None:
        break;
    }
    return {};
}

QString settingsHelp(QByteArrayView text, int position)
{
    const JsonParseResult parsed = parseJson(text);
    if (!parsed.root)
        return {};
    const std::optional<QStringList> path = pathAt(*parsed.root, position);
    if (!path)
        return {};
    const QString description = settingsSchema().description(*path);
    return description.isEmpty() ? QString() : QStringLiteral("%1\n%2").arg(keyNamed(*path), description);
}

} // namespace ClaudeLint
