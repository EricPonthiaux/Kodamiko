// SPDX-License-Identifier: GPL-3.0-only

#include "CMakePresets.h"

#include "system/Host.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <optional>

namespace {

using PresetsByName = QHash<QString, QJsonObject>;

// A cycle is an error CMake reports; the limit only keeps it from looping here.
constexpr int maxInheritanceDepth = 16;

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// The presets of `key`, hidden ones included, in file order.
QList<QJsonObject> readPresets(const QString &folder, const QString &key)
{
    QList<QJsonObject> presets;
    QSet<QString> names;
    for (const QString &file : {QStringLiteral("CMakePresets.json"), QStringLiteral("CMakeUserPresets.json")}) {
        for (const QJsonValue &value : readJson(folder + QLatin1Char('/') + file).value(key).toArray()) {
            const QJsonObject preset = value.toObject();
            const QString name = preset.value(QStringLiteral("name")).toString();
            if (name.isEmpty() || names.contains(name))
                continue;
            names.insert(name);
            presets.append(preset);
        }
    }
    return presets;
}

// CMake's ${hostSystemName}.
QString hostSystemName()
{
    switch (Host::operatingSystem()) {
    case OperatingSystem::Windows:
        return QStringLiteral("Windows");
    case OperatingSystem::MacOS:
        return QStringLiteral("Darwin");
    case OperatingSystem::Linux:
        return QStringLiteral("Linux");
    }
    return {};
}

// Only ${hostSystemName} is expanded: the macro that tells systems apart.
QString expandMacros(QString text)
{
    return text.replace(QStringLiteral("${hostSystemName}"), hostSystemName());
}

QString field(const QJsonObject &condition, QLatin1String key)
{
    return expandMacros(condition.value(key).toString());
}

bool isInList(const QJsonObject &condition)
{
    const QString string = field(condition, QLatin1String("string"));
    const QJsonArray list = condition.value(QLatin1String("list")).toArray();
    return std::any_of(list.begin(), list.end(),
                       [&string](const QJsonValue &item) { return expandMacros(item.toString()) == string; });
}

bool matchesRegex(const QJsonObject &condition)
{
    const QRegularExpression regex(field(condition, QLatin1String("regex")));
    return regex.match(field(condition, QLatin1String("string"))).hasMatch();
}

bool conditionHolds(const QJsonValue &value);

bool anyHolds(const QJsonObject &condition)
{
    const QJsonArray conditions = condition.value(QLatin1String("conditions")).toArray();
    return std::any_of(conditions.begin(), conditions.end(), conditionHolds);
}

bool allHold(const QJsonObject &condition)
{
    const QJsonArray conditions = condition.value(QLatin1String("conditions")).toArray();
    return std::all_of(conditions.begin(), conditions.end(), conditionHolds);
}

// https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html#condition
// An absent condition, or one of a type this does not know, holds.
bool conditionHolds(const QJsonValue &value)
{
    if (value.isBool())
        return value.toBool();
    const QJsonObject condition = value.toObject();
    const QString type = condition.value(QLatin1String("type")).toString();
    const bool sidesAreEqual = field(condition, QLatin1String("lhs")) == field(condition, QLatin1String("rhs"));
    if (type == QLatin1String("const"))
        return condition.value(QLatin1String("value")).toBool();
    if (type == QLatin1String("equals"))
        return sidesAreEqual;
    if (type == QLatin1String("notEquals"))
        return !sidesAreEqual;
    if (type == QLatin1String("inList"))
        return isInList(condition);
    if (type == QLatin1String("notInList"))
        return !isInList(condition);
    if (type == QLatin1String("matches"))
        return matchesRegex(condition);
    if (type == QLatin1String("notMatches"))
        return !matchesRegex(condition);
    if (type == QLatin1String("anyOf"))
        return anyHolds(condition);
    if (type == QLatin1String("allOf"))
        return allHold(condition);
    if (type == QLatin1String("not"))
        return !conditionHolds(condition.value(QLatin1String("condition")));
    return true;
}

QStringList parentNames(const QJsonObject &preset)
{
    const QJsonValue inherits = preset.value(QLatin1String("inherits"));
    if (inherits.isString())
        return {inherits.toString()};
    QStringList names;
    for (const QJsonValue &name : inherits.toArray())
        names.append(name.toString());
    return names;
}

// The preset's own condition, else the first one among its parents.
std::optional<QJsonValue> inheritedCondition(const QJsonObject &preset, const PresetsByName &presets, int depth)
{
    if (preset.contains(QLatin1String("condition")))
        return preset.value(QLatin1String("condition"));
    if (depth == maxInheritanceDepth)
        return std::nullopt;
    for (const QString &parent : parentNames(preset)) {
        if (std::optional<QJsonValue> found = inheritedCondition(presets.value(parent), presets, depth + 1))
            return found;
    }
    return std::nullopt;
}

// The presets of `key` a user can choose here, before checking their configure preset.
QList<CMakePreset> listedPresets(const QString &folder, const QString &key)
{
    const QList<QJsonObject> presets = readPresets(folder, key);
    PresetsByName byName;
    for (const QJsonObject &preset : presets)
        byName.insert(preset.value(QLatin1String("name")).toString(), preset);
    QList<CMakePreset> listed;
    for (const QJsonObject &preset : presets) {
        const bool isHidden = preset.value(QLatin1String("hidden")).toBool();
        if (isHidden || !conditionHolds(inheritedCondition(preset, byName, 0).value_or(QJsonValue())))
            continue;
        listed.append({.name = preset.value(QLatin1String("name")).toString(),
                       .configurePreset = preset.value(QLatin1String("configurePreset")).toString()});
    }
    return listed;
}

QList<CMakePreset> withListedConfigurePreset(QList<CMakePreset> presets, const QList<CMakePreset> &configure)
{
    QSet<QString> configureNames;
    for (const CMakePreset &preset : configure)
        configureNames.insert(preset.name);
    presets.removeIf([&configureNames](const CMakePreset &preset) {
        return !preset.configurePreset.isEmpty() && !configureNames.contains(preset.configurePreset);
    });
    return presets;
}

} // namespace

CMakePresetLists CMakePresets::forThisSystem(const QString &projectFolder)
{
    CMakePresetLists lists;
    lists.configure = listedPresets(projectFolder, QStringLiteral("configurePresets"));
    lists.build =
        withListedConfigurePreset(listedPresets(projectFolder, QStringLiteral("buildPresets")), lists.configure);
    lists.test =
        withListedConfigurePreset(listedPresets(projectFolder, QStringLiteral("testPresets")), lists.configure);
    return lists;
}
