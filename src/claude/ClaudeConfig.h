#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>

// Where Claude Code reads its configuration from, for the Claude panel:
// memory (CLAUDE.md files), rules, skills, agents, commands, output styles,
// settings and MCP servers, at each level (managed, user, project, local).
//
// Only reads the disk; files are created on request (newEntryPath(),
// templateFor()) and edited in the editor like any file.
namespace ClaudeConfig {

enum class Category {
    Memory,
    Rules,
    Skills,
    Agents,
    Commands,
    OutputStyles,
    Settings,
    Mcp
};

// From the lowest precedence to the highest, Duet's session settings last.
enum class Scope {
    Managed,   // set by the administrator, read-only
    User,      // ~/.claude, for every project
    Parent,    // CLAUDE.md of a folder above the project
    Auto,      // memory Claude writes itself, per project
    Project,   // in the project, shared through git
    Subfolder, // CLAUDE.md of a folder in the project, read when Claude works there
    Local,     // in the project, not shared
    Session,   // what Duet passes with --settings, read-only
};

struct Entry {
    Category category = Category::Memory;
    Scope scope = Scope::User;
    // Shown in the panel: "CLAUDE.md", "release-notes", "/deploy",
    // "src/CLAUDE.md"...
    QString name;
    QString path;        // absolute; for a skill, its SKILL.md
    QString description; // from the front matter, for skills, agents...
    bool exists = true;  // false: a usual file not created yet
    bool readOnly = false;
};

// Claude Code's configuration folder: $CLAUDE_CONFIG_DIR, or ~/.claude.
QString configDir();
// The folder Claude keeps its own memory of `projectDir` in.
QString autoMemoryDir(const QString &projectDir);

// Everything Claude Code reads for `projectDir`, grouped by category in
// the order of Category, then by scope. `sessionSettings` is the file
// Duet passes with --settings (may be empty).
QList<Entry> entries(const QString &projectDir, const QString &sessionSettings);

struct SettingsFile {
    Scope scope;
    QString path;
};
// The settings files Claude Code merges, from the lowest precedence to the
// highest: User, Project, Local, Duet's --settings (`sessionSettings`, if
// not empty), Managed; existing or not.
QList<SettingsFile> settingsFiles(const QString &projectDir, const QString &sessionSettings);

QString categoryTitle(Category category);
QString scopeTitle(Scope scope);
QString scopeDescription(Scope scope);

// The "key: value" lines between the "---" lines that open a Markdown
// file; empty without them.
QMap<QString, QString> frontMatter(const QByteArray &text);

// Categories whose items are made with New (skills, agents, commands,
// rules) and the scopes they can be made in (Project or User).
bool canCreate(Category category);
// Lowercase letters, digits and hyphens: what Claude Code accepts as a
// skill or agent name.
bool isValidName(const QString &name);
// Where a new item of `category` named `name` goes.
QString newEntryPath(Category category, Scope scope, const QString &projectDir, const QString &name);
// Initial content of a new item, or of a usual file not created yet.
QByteArray templateFor(const Entry &entry);

} // namespace ClaudeConfig
