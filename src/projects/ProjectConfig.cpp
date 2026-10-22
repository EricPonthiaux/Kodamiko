// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectConfig.h"

#include <QFileInfo>

QVariantMap LanguageSetup::toVariant() const
{
    return {
        {QStringLiteral("language"), language},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("runtimeVersion"), runtimeVersion},
        {QStringLiteral("runtimePath"), runtimePath},
        {QStringLiteral("server"), server},
        {QStringLiteral("serverVersion"), serverVersion},
        {QStringLiteral("recipe"), recipe},
    };
}

LanguageSetup LanguageSetup::fromVariant(const QVariantMap &map)
{
    LanguageSetup setup;
    setup.language = map.value(QStringLiteral("language")).toString();
    setup.enabled = map.value(QStringLiteral("enabled"), true).toBool();
    setup.runtimeVersion = map.value(QStringLiteral("runtimeVersion")).toString();
    setup.runtimePath = map.value(QStringLiteral("runtimePath")).toString();
    setup.server = map.value(QStringLiteral("server")).toString();
    setup.serverVersion = map.value(QStringLiteral("serverVersion")).toString();
    setup.recipe = map.value(QStringLiteral("recipe")).toString();
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
    for (const LanguageSetup &setup : languages) {
        if (setup.language == family)
            return &setup;
    }
    return nullptr;
}

LanguageSetup *ProjectConfig::language(const QString &family)
{
    for (LanguageSetup &setup : languages) {
        if (setup.language == family)
            return &setup;
    }
    return nullptr;
}

QVariantMap ProjectConfig::toVariant() const
{
    QVariantList list;
    for (const LanguageSetup &setup : languages)
        list.append(setup.toVariant());
    return {
        {QStringLiteral("lastOpened"), lastOpened},
        {QStringLiteral("configured"), configured},
        {QStringLiteral("languages"), list},
        {QStringLiteral("needsRecipeSetup"), needsRecipeSetup},
        {QStringLiteral("agentCheckCommand"), agentCheckCommand},
    };
}

ProjectConfig ProjectConfig::fromVariant(const QString &path, const QVariantMap &map)
{
    ProjectConfig config;
    config.path = path;
    config.name = QFileInfo(path).fileName();
    config.lastOpened = map.value(QStringLiteral("lastOpened")).toDateTime();
    config.configured = map.value(QStringLiteral("configured")).toBool();
    for (const QVariant &setup : map.value(QStringLiteral("languages")).toList())
        config.languages.append(LanguageSetup::fromVariant(setup.toMap()));
    config.agentCheckCommand = map.value(QStringLiteral("agentCheckCommand")).toString();
    config.needsRecipeSetup = map.value(QStringLiteral("needsRecipeSetup")).toBool();
    return config;
}

namespace ProjectLanguages {

QStringList families()
{
    return {QStringLiteral("java"), QStringLiteral("javascript"), QStringLiteral("cpp"), QStringLiteral("python")};
}

QString familyOf(const QString &languageId)
{
    if (languageId == QLatin1String("c") || languageId == QLatin1String("cpp"))
        return QStringLiteral("cpp");
    if (languageId == QLatin1String("javascript") || languageId == QLatin1String("typescript") ||
        languageId == QLatin1String("tsx"))
        return QStringLiteral("javascript");
    if (languageId == QLatin1String("java") || languageId == QLatin1String("python"))
        return languageId;
    return {};
}

QString title(const QString &family)
{
    if (family == QLatin1String("cpp"))
        return QStringLiteral("C/C++");
    if (family == QLatin1String("java"))
        return QStringLiteral("Java");
    if (family == QLatin1String("javascript"))
        return QStringLiteral("JavaScript/TypeScript");
    if (family == QLatin1String("python"))
        return QStringLiteral("Python");
    return family;
}

} // namespace ProjectLanguages
