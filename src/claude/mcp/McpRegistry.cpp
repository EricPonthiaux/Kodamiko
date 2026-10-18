#include "McpRegistry.h"

#include <QRegularExpression>

McpResult McpResult::json(const QJsonValue &value, const QString &text)
{
    McpResult result;
    result.structured = value;
    result.text = text;
    return result;
}

McpResult McpResult::message(const QString &text)
{
    McpResult result;
    result.text = text;
    return result;
}

McpResult McpResult::error(const QString &text)
{
    McpResult result;
    result.text = text;
    result.isError = true;
    return result;
}

McpTool McpTool::reading(const QString &name, const QString &title, const QString &description,
                         const QJsonObject &inputSchema, Run run)
{
    McpTool tool;
    tool.name = name;
    tool.title = title;
    tool.description = description;
    tool.inputSchema = inputSchema;
    tool.readOnly = true;
    tool.idempotent = true;
    tool.run = std::move(run);
    return tool;
}

McpTool McpTool::acting(const QString &name, const QString &title, const QString &description,
                        const QJsonObject &inputSchema, Run run, bool idempotent)
{
    McpTool tool = reading(name, title, description, inputSchema, std::move(run));
    tool.readOnly = false;
    tool.idempotent = idempotent;
    return tool;
}

McpTool McpTool::sensitive(const QString &name, const QString &title, const QString &description,
                           const QJsonObject &inputSchema, std::function<QString(const QJsonObject &)> describe,
                           Run run)
{
    McpTool tool = acting(name, title, description, inputSchema, std::move(run));
    tool.destructive = true;
    tool.confirm = true;
    tool.describe = std::move(describe);
    return tool;
}

McpTool::Run McpTool::immediate(std::function<McpResult(const QJsonObject &)> function)
{
    return [function = std::move(function)](const QJsonObject &arguments, McpReply reply, McpProgress) {
        reply(function(arguments));
    };
}

McpRegistry::McpRegistry(QObject *parent)
    : QObject(parent)
{
}

void McpRegistry::addTool(McpTool tool)
{
    tools_.append(std::move(tool));
}

void McpRegistry::addResource(McpResource resource)
{
    resources_.append(std::move(resource));
}

void McpRegistry::addPrompt(McpPrompt prompt)
{
    prompts_.append(std::move(prompt));
}

const McpTool *McpRegistry::tool(const QString &name) const
{
    for (const McpTool &tool : tools_) {
        if (tool.name == name)
            return &tool;
    }
    return nullptr;
}

const McpPrompt *McpRegistry::prompt(const QString &name) const
{
    for (const McpPrompt &prompt : prompts_) {
        if (prompt.name == name)
            return &prompt;
    }
    return nullptr;
}

const McpResource *McpRegistry::resourceFor(const QString &uri) const
{
    for (const McpResource &resource : resources_) {
        if (!resource.isTemplate && resource.uri == uri)
            return &resource;
    }
    for (const McpResource &resource : resources_) {
        if (!resource.isTemplate)
            continue;
        // "duet://diagram/{path}": the part before the variable must match.
        const qsizetype variable = resource.uri.indexOf(QLatin1Char('{'));
        if (variable > 0 && uri.startsWith(resource.uri.left(variable)) && uri.size() > variable)
            return &resource;
    }
    return nullptr;
}

void McpRegistry::confirm(const QString &tool, const QString &what, std::function<void(bool)> answer) const
{
    if (!confirmer_) {
        answer(false);
        return;
    }
    confirmer_(tool, what, std::move(answer));
}

QStringList McpRegistry::readOnlyToolPermissions(const QString &server) const
{
    QStringList names;
    for (const McpTool &tool : tools_) {
        if (tool.readOnly && !tool.confirm)
            names.append(QStringLiteral("mcp__%1__%2").arg(server, tool.name));
    }
    return names;
}

namespace {

bool hasType(const QJsonValue &value, const QString &type)
{
    if (type == QLatin1String("string"))
        return value.isString();
    if (type == QLatin1String("boolean"))
        return value.isBool();
    if (type == QLatin1String("number"))
        return value.isDouble();
    if (type == QLatin1String("integer"))
        return value.isDouble() && value.toDouble() == double(qint64(value.toDouble()));
    if (type == QLatin1String("array"))
        return value.isArray();
    if (type == QLatin1String("object"))
        return value.isObject();
    return true;
}

} // namespace

QString McpRegistry::validate(const QJsonObject &schema, const QJsonObject &arguments)
{
    for (const QJsonValue &name : schema.value(QLatin1String("required")).toArray()) {
        if (!arguments.contains(name.toString()))
            return QStringLiteral("Missing argument \"%1\"").arg(name.toString());
    }
    const QJsonObject properties = schema.value(QLatin1String("properties")).toObject();
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        const QJsonObject property = properties.value(it.key()).toObject();
        if (property.isEmpty())
            continue; // unknown arguments are ignored
        const QString type = property.value(QLatin1String("type")).toString();
        if (!type.isEmpty() && !hasType(it.value(), type))
            return QStringLiteral("Argument \"%1\" must be of type %2").arg(it.key(), type);
        const QJsonArray values = property.value(QLatin1String("enum")).toArray();
        if (!values.isEmpty() && !values.contains(it.value())) {
            QStringList allowed;
            for (const QJsonValue &value : values)
                allowed.append(value.toString());
            return QStringLiteral("Argument \"%1\" must be one of: %2")
                .arg(it.key(), allowed.join(QStringLiteral(", ")));
        }
    }
    return {};
}

namespace McpSchema {

QJsonObject object(const QJsonObject &properties, const QStringList &required)
{
    QJsonObject schema{{QLatin1String("type"), QLatin1String("object")}, {QLatin1String("properties"), properties}};
    if (!required.isEmpty())
        schema.insert(QLatin1String("required"), QJsonArray::fromStringList(required));
    return schema;
}

QJsonObject string(const QString &description)
{
    return {{QLatin1String("type"), QLatin1String("string")}, {QLatin1String("description"), description}};
}

QJsonObject integer(const QString &description)
{
    return {{QLatin1String("type"), QLatin1String("integer")}, {QLatin1String("description"), description}};
}

QJsonObject number(const QString &description)
{
    return {{QLatin1String("type"), QLatin1String("number")}, {QLatin1String("description"), description}};
}

QJsonObject boolean(const QString &description)
{
    return {{QLatin1String("type"), QLatin1String("boolean")}, {QLatin1String("description"), description}};
}

QJsonObject enumeration(const QStringList &values, const QString &description)
{
    return {{QLatin1String("type"), QLatin1String("string")},
            {QLatin1String("enum"), QJsonArray::fromStringList(values)},
            {QLatin1String("description"), description}};
}

QJsonObject array(const QJsonObject &items, const QString &description)
{
    return {{QLatin1String("type"), QLatin1String("array")},
            {QLatin1String("items"), items},
            {QLatin1String("description"), description}};
}

} // namespace McpSchema
