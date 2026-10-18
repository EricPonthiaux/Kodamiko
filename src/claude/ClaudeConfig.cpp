#include "ClaudeConfig.h"

#include "workspace/ProjectFiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace ClaudeConfig {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("ClaudeConfig", text);
}

QString managedDir()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("C:/Program Files/ClaudeCode");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("/Library/Application Support/ClaudeCode");
#else
    return QStringLiteral("/etc/claude-code");
#endif
}

// The file holding Claude Code's state, user MCP servers included.
QString stateFile()
{
    return qEnvironmentVariableIsSet("CLAUDE_CONFIG_DIR") ? QDir(configDir()).filePath(QStringLiteral(".claude.json"))
                                                          : QDir::home().filePath(QStringLiteral(".claude.json"));
}

QByteArray readHead(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.read(16 * 1024); // the front matter is at the top
}

// Markdown files under `dir`, sorted, as paths relative to it.
QStringList markdownFiles(const QString &dir, bool recursive)
{
    QStringList files;
    QDirIterator it(dir, {QStringLiteral("*.md")}, QDir::Files,
                    recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext())
        files.append(QDir(dir).relativeFilePath(it.next()));
    files.sort(Qt::CaseInsensitive);
    return files;
}

class Collector {
public:
    QList<Entry> entries;

    // A file that is listed only when it exists.
    void add(Category category, Scope scope, const QString &name, const QString &path, bool readOnly = false)
    {
        if (QFileInfo(path).isFile())
            entries.append(makeEntry(category, scope, name, path, true, readOnly));
    }

    // A usual file, listed even before it is created.
    void addUsual(Category category, Scope scope, const QString &name, const QString &path)
    {
        entries.append(makeEntry(category, scope, name, path, QFileInfo(path).isFile(), false));
    }

    // Skills: one folder each, with its SKILL.md.
    void addSkills(Scope scope, const QString &base)
    {
        const QDir dir(QDir(base).filePath(QStringLiteral("skills")));
        const QStringList names = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QString &name : names)
            add(Category::Skills, scope, name, dir.filePath(name + QStringLiteral("/SKILL.md")));
    }

    void addMarkdown(Category category, Scope scope, const QString &dir, bool recursive)
    {
        for (const QString &relative : markdownFiles(dir, recursive)) {
            QString name = relative;
            if (category == Category::Commands)
                name = QLatin1Char('/') + relative.chopped(3);
            else if (category != Category::Rules)
                name = relative.chopped(3);
            add(category, scope, name, QDir(dir).filePath(relative));
        }
    }

private:
    static Entry makeEntry(Category category, Scope scope, const QString &name, const QString &path, bool exists,
                           bool readOnly)
    {
        Entry entry;
        entry.category = category;
        entry.scope = scope;
        entry.name = name;
        entry.path = QDir::cleanPath(path);
        entry.exists = exists;
        entry.readOnly = readOnly || scope == Scope::Managed || scope == Scope::Session;
        if (exists && path.endsWith(QLatin1String(".md"))) {
            const QMap<QString, QString> fields = frontMatter(readHead(path));
            entry.description = fields.value(QStringLiteral("description"));
            if (category == Category::Skills || category == Category::Agents) {
                if (const QString declared = fields.value(QStringLiteral("name")); !declared.isEmpty())
                    entry.name = declared;
            }
        }
        return entry;
    }
};

} // namespace

QString configDir()
{
    return qEnvironmentVariableIsSet("CLAUDE_CONFIG_DIR") ? qEnvironmentVariable("CLAUDE_CONFIG_DIR")
                                                          : QDir::home().filePath(QStringLiteral(".claude"));
}

QString autoMemoryDir(const QString &projectDir)
{
    // Claude Code names the folder after the path, every other character
    // than a letter or a digit replaced: "/home/me/app" → "-home-me-app".
    static const QRegularExpression other(QStringLiteral("[^A-Za-z0-9]"));
    QString slug = QDir::cleanPath(QFileInfo(projectDir).absoluteFilePath());
    slug.replace(other, QStringLiteral("-"));
    return QDir(configDir()).filePath(QStringLiteral("projects/%1/memory").arg(slug));
}

QList<Entry> entries(const QString &projectDir, const QString &sessionSettings)
{
    const QDir user(configDir());
    const QDir project(projectDir);
    const QDir projectClaude(project.filePath(QStringLiteral(".claude")));
    const QDir managed(managedDir());
    Collector c;

    // Memory.
    c.add(Category::Memory, Scope::Managed, QStringLiteral("CLAUDE.md"), managed.filePath(QStringLiteral("CLAUDE.md")));
    c.addUsual(Category::Memory, Scope::User, QStringLiteral("CLAUDE.md"), user.filePath(QStringLiteral("CLAUDE.md")));
    QStringList parents;
    for (QDir dir(project.absolutePath()); dir.cdUp();)
        parents.prepend(dir.absolutePath());
    for (const QString &parent : std::as_const(parents)) {
        for (const QString &fileName : {QStringLiteral("CLAUDE.md"), QStringLiteral("CLAUDE.local.md")}) {
            const QString path = QDir(parent).filePath(fileName);
            c.add(Category::Memory, Scope::Parent, QDir::toNativeSeparators(QDir::cleanPath(path)), path);
        }
    }
    const QString autoMemory = autoMemoryDir(projectDir);
    const QStringList memories = markdownFiles(autoMemory, false);
    if (memories.contains(QStringLiteral("MEMORY.md")))
        c.add(Category::Memory, Scope::Auto, QStringLiteral("MEMORY.md"),
              QDir(autoMemory).filePath(QStringLiteral("MEMORY.md")));
    for (const QString &memory : memories) {
        if (memory != QLatin1String("MEMORY.md"))
            c.add(Category::Memory, Scope::Auto, memory, QDir(autoMemory).filePath(memory));
    }
    if (projectClaude.exists(QStringLiteral("CLAUDE.md")) && !project.exists(QStringLiteral("CLAUDE.md")))
        c.add(Category::Memory, Scope::Project, QStringLiteral(".claude/CLAUDE.md"),
              projectClaude.filePath(QStringLiteral("CLAUDE.md")));
    else
        c.addUsual(Category::Memory, Scope::Project, QStringLiteral("CLAUDE.md"),
                   project.filePath(QStringLiteral("CLAUDE.md")));
    ProjectFiles::Options options;
    options.accept = [](const QFileInfo &info) {
        return info.fileName() == QLatin1String("CLAUDE.md") || info.fileName() == QLatin1String("CLAUDE.local.md");
    };
    for (const QFileInfo &info : ProjectFiles::list(projectDir, options)) {
        if (info.absolutePath() != project.absolutePath())
            c.add(Category::Memory, Scope::Subfolder, project.relativeFilePath(info.absoluteFilePath()),
                  info.absoluteFilePath());
    }
    c.addUsual(Category::Memory, Scope::Local, QStringLiteral("CLAUDE.local.md"),
               project.filePath(QStringLiteral("CLAUDE.local.md")));

    for (const auto &[scope, base] : {std::pair{Scope::User, user}, std::pair{Scope::Project, projectClaude}}) {
        c.addMarkdown(Category::Rules, scope, base.filePath(QStringLiteral("rules")), true);
        c.addSkills(scope, base.absolutePath());
        c.addMarkdown(Category::Agents, scope, base.filePath(QStringLiteral("agents")), false);
        c.addMarkdown(Category::Commands, scope, base.filePath(QStringLiteral("commands")), true);
        c.addMarkdown(Category::OutputStyles, scope, base.filePath(QStringLiteral("output-styles")), false);
    }

    // Settings.
    for (const SettingsFile &file : settingsFiles(projectDir, sessionSettings)) {
        switch (file.scope) {
        case Scope::Managed:
            c.add(Category::Settings, file.scope, QFileInfo(file.path).fileName(), file.path);
            break;
        case Scope::Session:
            c.add(Category::Settings, file.scope, tr("Duet session hooks"), file.path);
            break;
        case Scope::User:
            c.addUsual(Category::Settings, file.scope, QFileInfo(file.path).fileName(), file.path);
            break;
        default:
            c.addUsual(Category::Settings, file.scope, project.relativeFilePath(file.path), file.path);
            break;
        }
    }
    c.add(Category::Settings, Scope::User, QStringLiteral("keybindings.json"),
          user.filePath(QStringLiteral("keybindings.json")));

    // MCP servers. Claude Code rewrites its state file all the time: it
    // is shown, never edited (`claude mcp` changes it).
    c.add(Category::Mcp, Scope::Managed, QStringLiteral("managed-mcp.json"),
          managed.filePath(QStringLiteral("managed-mcp.json")));
    c.add(Category::Mcp, Scope::User, QFileInfo(stateFile()).fileName(), stateFile(), true);
    c.addUsual(Category::Mcp, Scope::Project, QStringLiteral(".mcp.json"),
               project.filePath(QStringLiteral(".mcp.json")));

    std::stable_sort(c.entries.begin(), c.entries.end(), [](const Entry &a, const Entry &b) {
        if (a.category != b.category)
            return a.category < b.category;
        return a.scope < b.scope;
    });
    return c.entries;
}

QList<SettingsFile> settingsFiles(const QString &projectDir, const QString &sessionSettings)
{
    const QDir projectClaude(QDir(projectDir).filePath(QStringLiteral(".claude")));
    QList<SettingsFile> files{
        {Scope::User, QDir(configDir()).filePath(QStringLiteral("settings.json"))},
        {Scope::Project, projectClaude.filePath(QStringLiteral("settings.json"))},
        {Scope::Local, projectClaude.filePath(QStringLiteral("settings.local.json"))},
    };
    if (!sessionSettings.isEmpty())
        files.append({Scope::Session, sessionSettings});
    files.append({Scope::Managed, QDir(managedDir()).filePath(QStringLiteral("managed-settings.json"))});
    return files;
}

QString categoryTitle(Category category)
{
    switch (category) {
    case Category::Memory:
        return tr("Memory");
    case Category::Rules:
        return tr("Rules");
    case Category::Skills:
        return tr("Skills");
    case Category::Agents:
        return tr("Agents");
    case Category::Commands:
        return tr("Commands");
    case Category::OutputStyles:
        return tr("Output Styles");
    case Category::Settings:
        return tr("Settings");
    case Category::Mcp:
        return tr("MCP Servers");
    }
    return {};
}

QString scopeTitle(Scope scope)
{
    switch (scope) {
    case Scope::Managed:
        return tr("Managed");
    case Scope::User:
        return tr("User");
    case Scope::Parent:
        return tr("Parent");
    case Scope::Auto:
        return tr("Auto");
    case Scope::Project:
        return tr("Project");
    case Scope::Subfolder:
        return tr("Folder");
    case Scope::Local:
        return tr("Local");
    case Scope::Session:
        return tr("Duet");
    }
    return {};
}

QString scopeDescription(Scope scope)
{
    switch (scope) {
    case Scope::Managed:
        return tr("Set by your administrator, read-only");
    case Scope::User:
        return tr("Yours, for every project");
    case Scope::Parent:
        return tr("From a folder above the project");
    case Scope::Auto:
        return tr("Written by Claude itself, for this project");
    case Scope::Project:
        return tr("The project's, shared through git");
    case Scope::Subfolder:
        return tr("Read when Claude works in this folder");
    case Scope::Local:
        return tr("Yours, for this project only, not shared");
    case Scope::Session:
        return tr("Passed by Duet to its Claude session, read-only");
    }
    return {};
}

QMap<QString, QString> frontMatter(const QByteArray &text)
{
    QMap<QString, QString> fields;
    const QStringList lines = QString::fromUtf8(text).split(QLatin1Char('\n'));
    if (lines.isEmpty() || lines.first().trimmed() != QLatin1String("---"))
        return fields;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line == QLatin1String("---"))
            break;
        const qsizetype colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0 || lines[i].front().isSpace())
            continue;
        QString value = line.mid(colon + 1).trimmed();
        if (value.size() >= 2 && (value.front() == QLatin1Char('"') || value.front() == QLatin1Char('\'')) &&
            value.back() == value.front())
            value = value.mid(1, value.size() - 2);
        fields.insert(line.left(colon).trimmed(), value);
    }
    return fields;
}

bool canCreate(Category category)
{
    return category == Category::Skills || category == Category::Agents || category == Category::Commands ||
           category == Category::Rules;
}

bool isValidName(const QString &name)
{
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9]+(-[a-z0-9]+)*$"));
    return name.size() <= 64 && valid.match(name).hasMatch();
}

QString newEntryPath(Category category, Scope scope, const QString &projectDir, const QString &name)
{
    const QDir base(scope == Scope::User ? configDir() : QDir(projectDir).filePath(QStringLiteral(".claude")));
    switch (category) {
    case Category::Skills:
        return base.filePath(QStringLiteral("skills/%1/SKILL.md").arg(name));
    case Category::Agents:
        return base.filePath(QStringLiteral("agents/%1.md").arg(name));
    case Category::Commands:
        return base.filePath(QStringLiteral("commands/%1.md").arg(name));
    case Category::Rules:
        return base.filePath(QStringLiteral("rules/%1.md").arg(name));
    default:
        return {};
    }
}

QByteArray templateFor(const Entry &entry)
{
    const QByteArray name = entry.name.toUtf8();
    switch (entry.category) {
    case Category::Memory:
        return entry.scope == Scope::Project ? QByteArrayLiteral("# Project instructions\n\n")
                                             : QByteArrayLiteral("# My instructions\n\n");
    case Category::Skills:
        return "---\nname: " + name + "\ndescription: What this skill does, and when Claude should use it.\n---\n\n# " +
               name + "\n\n";
    case Category::Agents:
        return "---\nname: " + name +
               "\ndescription: When Claude should hand a task to this agent.\n---\n\n"
               "You are an agent that…\n";
    case Category::Commands:
        return QByteArrayLiteral("---\ndescription: What /") + name +
               " does.\n---\n\nThe prompt sent to Claude. $ARGUMENTS stands for what follows the command.\n";
    case Category::Rules:
        return "# " + name + "\n\n";
    case Category::Settings:
        return QByteArrayLiteral("{\n    \"$schema\": \"https://json.schemastore.org/claude-code-settings.json\"\n}\n");
    case Category::Mcp:
        return QByteArrayLiteral("{\n    \"mcpServers\": {\n    }\n}\n");
    case Category::OutputStyles:
        break;
    }
    return {};
}

} // namespace ClaudeConfig
