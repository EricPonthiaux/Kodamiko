#include "ProjectStore.h"

#include <QDir>
#include <QSettings>

#include <algorithm>

namespace {

const QString projectsKey = QStringLiteral("projects");

QVariantMap load()
{
    QSettings settings;
    if (!settings.contains(projectsKey)) {
        // Once: the folders opened before projects existed, in their order.
        QVariantMap projects;
        const QStringList recent = settings.value(QStringLiteral("recentFolders")).toStringList();
        const QDateTime now = QDateTime::currentDateTime();
        for (qsizetype i = 0; i < recent.size(); ++i) {
            ProjectConfig config;
            config.lastOpened = now.addSecs(-i);
            projects.insert(recent[i], config.toVariant());
        }
        settings.setValue(projectsKey, projects);
        return projects;
    }
    return settings.value(projectsKey).toMap();
}

void store(const QVariantMap &projects)
{
    QSettings().setValue(projectsKey, projects);
}

} // namespace

namespace ProjectStore {

QList<ProjectConfig> all()
{
    const QVariantMap projects = load();
    QList<ProjectConfig> configs;
    for (auto it = projects.begin(); it != projects.end(); ++it)
        configs.append(ProjectConfig::fromVariant(it.key(), it.value().toMap()));
    std::stable_sort(configs.begin(), configs.end(),
                     [](const ProjectConfig &a, const ProjectConfig &b) { return a.lastOpened > b.lastOpened; });
    return configs;
}

std::optional<ProjectConfig> find(const QString &path)
{
    const QString key = QDir(path).absolutePath();
    const QVariantMap projects = load();
    if (!projects.contains(key))
        return std::nullopt;
    return ProjectConfig::fromVariant(key, projects.value(key).toMap());
}

void save(const ProjectConfig &config)
{
    QVariantMap projects = load();
    projects.insert(QDir(config.path).absolutePath(), config.toVariant());
    store(projects);
}

void remove(const QString &path)
{
    QVariantMap projects = load();
    projects.remove(QDir(path).absolutePath());
    store(projects);
}

ProjectConfig touch(const QString &path)
{
    const QString key = QDir(path).absolutePath();
    ProjectConfig config = find(key).value_or(ProjectConfig::fromVariant(key, {}));
    config.lastOpened = QDateTime::currentDateTime();
    save(config);
    return config;
}

} // namespace ProjectStore
