// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectConfig.h"

#include <QFileInfo>

#include <algorithm>
#include <array>

namespace {

struct Family {
    QLatin1StringView id;
    QLatin1StringView title;
};

struct LanguageFamily {
    QLatin1StringView languageId; // a LanguageRegistry identifier
    QLatin1StringView family;
};

// In the order the project setup shows them.
constexpr std::array knownFamilies = {
    Family{QLatin1StringView("java"), QLatin1StringView("Java")},
    Family{QLatin1StringView("javascript"), QLatin1StringView("JavaScript/TypeScript")},
    Family{QLatin1StringView("cpp"), QLatin1StringView("C/C++")},
    Family{QLatin1StringView("python"), QLatin1StringView("Python")},
};

constexpr std::array languageFamilies = {
    LanguageFamily{QLatin1StringView("c"), QLatin1StringView("cpp")},
    LanguageFamily{QLatin1StringView("cpp"), QLatin1StringView("cpp")},
    LanguageFamily{QLatin1StringView("java"), QLatin1StringView("java")},
    LanguageFamily{QLatin1StringView("javascript"), QLatin1StringView("javascript")},
    LanguageFamily{QLatin1StringView("typescript"), QLatin1StringView("javascript")},
    LanguageFamily{QLatin1StringView("tsx"), QLatin1StringView("javascript")},
    LanguageFamily{QLatin1StringView("python"), QLatin1StringView("python")},
};

// The keys of the saved maps, shared by toVariant and fromVariant so that a
// field is read back under the name it was written with.
constexpr QLatin1StringView languageKey("language");
constexpr QLatin1StringView enabledKey("enabled");
constexpr QLatin1StringView runtimeVersionKey("runtimeVersion");
constexpr QLatin1StringView runtimePathKey("runtimePath");
constexpr QLatin1StringView serverKey("server");
constexpr QLatin1StringView serverVersionKey("serverVersion");
constexpr QLatin1StringView recipeKey("recipe");
constexpr QLatin1StringView lastOpenedKey("lastOpened");
constexpr QLatin1StringView configuredKey("configured");
constexpr QLatin1StringView languagesKey("languages");
constexpr QLatin1StringView needsRecipeSetupKey("needsRecipeSetup");
constexpr QLatin1StringView agentCheckCommandKey("agentCheckCommand");
constexpr QLatin1StringView recipeValuesKey("recipeValues");

QVariantMap variantOf(const QMap<QString, QString> &map)
{
    QVariantMap variant;
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        variant.insert(it.key(), it.value());
    return variant;
}

} // namespace

QVariantMap LanguageSetup::toVariant() const
{
    return {
        {languageKey, language},       {enabledKey, enabled}, {runtimeVersionKey, runtimeVersion},
        {runtimePathKey, runtimePath}, {serverKey, server},   {serverVersionKey, serverVersion},
        {recipeKey, recipe},
    };
}

LanguageSetup LanguageSetup::fromVariant(const QVariantMap &map)
{
    LanguageSetup setup;
    setup.language = map.value(languageKey).toString();
    setup.enabled = map.value(enabledKey, true).toBool();
    setup.runtimeVersion = map.value(runtimeVersionKey).toString();
    setup.runtimePath = map.value(runtimePathKey).toString();
    setup.server = map.value(serverKey).toString();
    setup.serverVersion = map.value(serverVersionKey).toString();
    setup.recipe = map.value(recipeKey).toString();
    return setup;
}

QStringList ProjectConfig::recipes() const
{
    QStringList ids;
    for (const LanguageSetup &setup : languages) {
        if (setup.enabled && !setup.recipe.isEmpty())
            ids.append(setup.recipe);
    }
    return ids;
}

const LanguageSetup *ProjectConfig::language(const QString &family) const
{
    const auto found = std::ranges::find(languages, family, &LanguageSetup::language);
    return found == languages.end() ? nullptr : &*found;
}

LanguageSetup *ProjectConfig::language(const QString &family)
{
    const auto found = std::ranges::find(languages, family, &LanguageSetup::language);
    return found == languages.end() ? nullptr : &*found;
}

QVariantMap ProjectConfig::toVariant() const
{
    QVariantList list;
    for (const LanguageSetup &setup : languages)
        list.append(setup.toVariant());
    return {
        {lastOpenedKey, lastOpened},
        {configuredKey, configured},
        {languagesKey, list},
        {agentCheckCommandKey, agentCheckCommand},
        {needsRecipeSetupKey, needsRecipeSetup},
        {recipeValuesKey, variantOf(recipeValues)},
    };
}

ProjectConfig ProjectConfig::fromVariant(const QString &path, const QVariantMap &map)
{
    ProjectConfig config;
    config.path = path;
    config.name = QFileInfo(path).fileName();
    config.lastOpened = map.value(lastOpenedKey).toDateTime();
    config.configured = map.value(configuredKey).toBool();
    for (const QVariant &entry : map.value(languagesKey).toList())
        config.languages.append(LanguageSetup::fromVariant(entry.toMap()));
    config.agentCheckCommand = map.value(agentCheckCommandKey).toString();
    config.needsRecipeSetup = map.value(needsRecipeSetupKey).toBool();
    const QVariantMap values = map.value(recipeValuesKey).toMap();
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        config.recipeValues.insert(it.key(), it.value().toString());
    return config;
}

namespace ProjectLanguages {

QStringList families()
{
    QStringList ids;
    for (const Family &family : knownFamilies)
        ids.append(family.id);
    return ids;
}

QString familyOf(const QString &languageId)
{
    for (const LanguageFamily &entry : languageFamilies) {
        if (entry.languageId == languageId)
            return entry.family;
    }
    return {};
}

QString title(const QString &family)
{
    for (const Family &known : knownFamilies) {
        if (known.id == family)
            return known.title;
    }
    return family;
}

} // namespace ProjectLanguages
