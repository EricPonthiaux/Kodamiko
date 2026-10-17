#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

// What the editor offers its Claude session through MCP (the Model Context
// Protocol): tools Claude calls, resources it reads and follows, prompts the
// user picks as slash commands. Each domain of the editor (files and tabs,
// code, search, git, terminals, diagrams…) declares its own here; the
// sessions (McpSession, one per connected relay) serve them.

// What a tool returns: structured content (shown to Claude as JSON) and a
// text, or an error Claude reads as the tool's result.
struct McpResult {
    QJsonValue structured;
    QString text;
    bool isError = false;

    static McpResult json(const QJsonValue &value, const QString &text = {});
    static McpResult message(const QString &text);
    static McpResult error(const QString &text);
};

using McpReply = std::function<void(const McpResult &)>;
// Progress of a long tool (a command running): progress, total (0 if
// unknown), message.
using McpProgress = std::function<void(double, double, const QString &)>;

struct McpTool {
    using Run = std::function<void(const QJsonObject &arguments, McpReply reply, McpProgress progress)>;

    // A tool that only reads.
    static McpTool reading(const QString &name, const QString &title, const QString &description,
                           const QJsonObject &inputSchema, Run run);
    // A tool that acts in the editor (opens, lays out…), without asking.
    static McpTool acting(const QString &name, const QString &title, const QString &description,
                          const QJsonObject &inputSchema, Run run, bool idempotent = false);
    // A tool that changes the project or runs something: the user is asked
    // first, with `describe`'s text.
    static McpTool sensitive(const QString &name, const QString &title, const QString &description,
                             const QJsonObject &inputSchema, std::function<QString(const QJsonObject &)> describe,
                             Run run);
    // A Run from a function answering at once.
    static Run immediate(std::function<McpResult(const QJsonObject &arguments)> function);

    QString name;        // "get_editor_state"
    QString title;       // "Editor state"
    QString description; // for Claude: what it does, when to use it
    QJsonObject inputSchema;
    QJsonObject outputSchema; // optional
    bool readOnly = false;
    bool destructive = false;
    bool idempotent = false;
    // Asks the user in the editor before running (Allow / Deny).
    bool confirm = false;
    // What the confirmation says the tool is about to do.
    std::function<QString(const QJsonObject &arguments)> describe;
    std::function<void(const QJsonObject &arguments, McpReply reply, McpProgress progress)> run;
};

struct McpResource {
    // A fixed URI ("duet://editor/state") or a template
    // ("duet://diagram/{path}").
    QString uri;
    bool isTemplate = false;
    QString name;
    QString title;
    QString description;
    QString mimeType = QStringLiteral("application/json");
    // The content of `uri` (a template's resources: any URI it matches),
    // nullopt if there is none.
    std::function<std::optional<QString>(const QString &uri)> read;
};

struct McpPromptArgument {
    QString name;
    QString description;
    bool required = false;
};

struct McpPrompt {
    QString name;
    QString title;
    QString description;
    QList<McpPromptArgument> arguments;
    // The prompt's text for these arguments.
    std::function<QString(const QJsonObject &arguments)> text;
};

class McpRegistry : public QObject {
    Q_OBJECT

public:
    explicit McpRegistry(QObject *parent = nullptr);

    void addTool(McpTool tool);
    void addResource(McpResource resource);
    void addPrompt(McpPrompt prompt);

    const QList<McpTool> &tools() const { return tools_; }
    const QList<McpResource> &resources() const { return resources_; }
    const QList<McpPrompt> &prompts() const { return prompts_; }
    const McpTool *tool(const QString &name) const;
    const McpPrompt *prompt(const QString &name) const;
    // The resource (fixed or template) serving `uri`.
    const McpResource *resourceFor(const QString &uri) const;

    // What the server tells Claude about itself when it connects.
    QString instructions;

    // Asks the user whether a tool may do `what`; `answer` is called once.
    // Without a confirmer, tools that need one are refused.
    using Confirmer = std::function<void(const QString &tool, const QString &what, std::function<void(bool)> answer)>;
    void setConfirmer(Confirmer confirmer) { confirmer_ = std::move(confirmer); }
    void confirm(const QString &tool, const QString &what, std::function<void(bool)> answer) const;

    // Names of the read-only tools, as Claude Code names them
    // ("mcp__duet__get_editor_state"): allowed without asking.
    QStringList readOnlyToolPermissions(const QString &server) const;

    // Checks arguments against a tool's input schema (required properties,
    // types, enumerations): the problem, or an empty string.
    static QString validate(const QJsonObject &schema, const QJsonObject &arguments);

signals:
    // A resource changed: subscribed sessions are told.
    void resourceUpdated(const QString &uri);

public:
    void notifyResourceUpdated(const QString &uri) { emit resourceUpdated(uri); }

private:
    QList<McpTool> tools_;
    QList<McpResource> resources_;
    QList<McpPrompt> prompts_;
    Confirmer confirmer_;
};

// JSON Schema helpers for tool declarations.
namespace McpSchema {

QJsonObject object(const QJsonObject &properties, const QStringList &required = {});
QJsonObject string(const QString &description);
QJsonObject integer(const QString &description);
QJsonObject number(const QString &description);
QJsonObject boolean(const QString &description);
QJsonObject enumeration(const QStringList &values, const QString &description);
QJsonObject array(const QJsonObject &items, const QString &description);

} // namespace McpSchema
