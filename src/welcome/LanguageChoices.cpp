// SPDX-License-Identifier: GPL-3.0-only

#include "LanguageChoices.h"

#include "toolchains/ToolCatalog.h"
#include "toolchains/Toolchains.h"

#include <QComboBox>
#include <QDir>
#include <QVersionNumber>

#include <algorithm>
#include <optional>

namespace LanguageChoices {
namespace {

const QString versionKey = QStringLiteral("version");
const QString pathKey = QStringLiteral("path");

QVariant runtimeData(const QString &version, const QString &path = {})
{
    return QVariantMap{{versionKey, version}, {pathKey, path}};
}

void addItem(QComboBox *combo, const QString &text, const QVariant &data, const QString &state)
{
    combo->addItem(text, data);
    combo->setItemData(combo->count() - 1, state, stateRole);
}

// The first item of `combo` for `version`, whatever its path; -1 if none.
int indexOfVersion(const QComboBox *combo, const QString &version)
{
    for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemData(i).toMap().value(versionKey) == version)
            return i;
    }
    return -1;
}

int fillCppStandards(QComboBox *combo, const LanguageSetup &setup)
{
    for (const QString &standard : ToolCatalog::runtimeVersions(QStringLiteral("cpp")))
        addItem(combo, QString(standard).replace(QLatin1String("c++"), QLatin1String("C++")), runtimeData(standard),
                {});
    return combo->findData(runtimeData(setup.runtimeVersion));
}

// `executable`, relative to the project when inside it.
QString pythonLocation(const QString &executable, const QString &projectPath)
{
    const bool isInProject = !projectPath.isEmpty() && executable.startsWith(projectPath + QLatin1Char('/'));
    if (isInProject)
        return QObject::tr("The project's %1").arg(QDir(projectPath).relativeFilePath(executable));
    return QDir::toNativeSeparators(executable);
}

int fillPythons(QComboBox *combo, const LanguageSetup &setup, const Toolchains &toolchains, const QString &projectPath)
{
    QList<RuntimeInstall> pythons = toolchains.runtimes(QStringLiteral("python"));
    const bool listed = std::ranges::any_of(
        pythons, [&](const RuntimeInstall &python) { return python.executable == setup.runtimePath; });
    if (!setup.runtimePath.isEmpty() && !listed)
        pythons.prepend({QStringLiteral("python"), setup.runtimeVersion, QFileInfo(setup.runtimePath).absolutePath(),
                         setup.runtimePath, false});
    for (const RuntimeInstall &python : std::as_const(pythons)) {
        const QString version = python.version.isEmpty() ? QObject::tr("?") : python.version;
        const QString location = pythonLocation(python.executable, projectPath);
        // Two of a version: where each is tells them apart.
        const auto sameVersion = [&python](const RuntimeInstall &other) { return other.version == python.version; };
        const bool isAmbiguous = std::ranges::count_if(pythons, sameVersion) > 1;
        const QString text =
            isAmbiguous ? QObject::tr("Python %1 (%2)").arg(version, location) : QObject::tr("Python %1").arg(version);
        addItem(combo, text, runtimeData(python.version, python.executable), QObject::tr("✓ %1").arg(location));
    }
    if (pythons.isEmpty())
        addItem(combo, QObject::tr("None found"), runtimeData(setup.runtimeVersion),
                QObject::tr("No Python on this machine; Duet does not download it."));
    const int selected = combo->findData(runtimeData(setup.runtimeVersion, setup.runtimePath));
    return selected >= 0 ? selected : indexOfVersion(combo, setup.runtimeVersion);
}

QString runtimeState(const std::optional<RuntimeInstall> &found)
{
    if (!found)
        return QObject::tr("Will download");
    if (found->managed)
        return QObject::tr("✓ Downloaded");
    return QObject::tr("✓ On this machine — %1").arg(QDir::toNativeSeparators(found->home));
}

// JDKs or Node.js versions: the catalog's, the machine's and `setup`'s.
int fillToolRuntimes(QComboBox *combo, const LanguageSetup &setup, const Toolchains &toolchains)
{
    const QString tool = ToolCatalog::runtimeOf(setup.language);
    const ToolSpec *spec = ToolCatalog::find(tool);
    QStringList versions = ToolCatalog::runtimeVersions(setup.language);
    for (const RuntimeInstall &runtime : toolchains.runtimes(tool)) {
        if (!versions.contains(runtime.version))
            versions.append(runtime.version);
    }
    if (!setup.runtimeVersion.isEmpty() && !versions.contains(setup.runtimeVersion))
        versions.append(setup.runtimeVersion);
    std::ranges::sort(versions, [](const QString &a, const QString &b) { return a.toInt() > b.toInt(); });
    const QString title = spec ? spec->title : tool;
    for (const QString &version : std::as_const(versions))
        addItem(combo, QObject::tr("%1 %2").arg(title, version), runtimeData(version),
                runtimeState(toolchains.runtime(tool, version)));
    return combo->findData(runtimeData(setup.runtimeVersion));
}

QString serverTitle(const QString &id)
{
    if (id.isEmpty())
        return QObject::tr("None");
    const ToolSpec *spec = ToolCatalog::find(id);
    return spec ? spec->title : id;
}

} // namespace

void fillRuntimes(QComboBox *combo,
                  const LanguageSetup &setup,
                  const Toolchains &toolchains,
                  const QString &projectPath)
{
    combo->clear();
    int selected = -1;
    if (setup.language == QLatin1String("cpp"))
        selected = fillCppStandards(combo, setup);
    else if (setup.language == QLatin1String("python"))
        selected = fillPythons(combo, setup, toolchains, projectPath);
    else
        selected = fillToolRuntimes(combo, setup, toolchains);
    combo->setCurrentIndex(std::max(0, selected));
}

Runtime selectedRuntime(const QComboBox *combo)
{
    const QVariantMap runtime = combo->currentData().toMap();
    return {runtime.value(versionKey).toString(), runtime.value(pathKey).toString()};
}

void fillServers(QComboBox *combo, const QString &family, const QString &selected)
{
    combo->clear();
    for (const QString &id : ToolCatalog::servers(family))
        combo->addItem(serverTitle(id), id);
    combo->addItem(serverTitle({}), QString());
    combo->setItemData(combo->count() - 1, QObject::tr("Words of the project only"), Qt::ToolTipRole);
    combo->setCurrentIndex(std::max(0, combo->findData(selected)));
}

void fillServerVersions(QComboBox *combo, const QString &server, const QString &selected, const Toolchains &toolchains)
{
    combo->clear();
    if (server.isEmpty())
        return;
    const QString system = toolchains.systemProgram(server);
    addItem(combo, QObject::tr("System"), QLatin1String(ProjectLanguages::systemVersion),
            system.isEmpty() ? QObject::tr("Not found in the PATH")
                             : QObject::tr("✓ On this machine — %1").arg(QDir::toNativeSeparators(system)));
    QStringList versions;
    if (const ToolSpec *spec = ToolCatalog::find(server))
        versions = spec->versions;
    for (const QString &version : toolchains.installedVersions(server)) {
        if (!versions.contains(version))
            versions.append(version);
    }
    std::ranges::sort(versions, [](const QString &a, const QString &b) {
        return QVersionNumber::fromString(a) > QVersionNumber::fromString(b);
    });
    for (const QString &version : std::as_const(versions))
        addItem(combo, version, version,
                toolchains.isInstalled(server, version) ? QObject::tr("✓ Downloaded") : QObject::tr("Will download"));
    combo->setCurrentIndex(std::max(0, combo->findData(selected)));
}

} // namespace LanguageChoices
