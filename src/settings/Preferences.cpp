#include "Preferences.h"

#include "ui/AppFonts.h"

#include <QSettings>

#include <algorithm>

Preferences Preferences::load()
{
    const QSettings settings;
    Preferences preferences;
    if (settings.contains(QStringLiteral("theme")))
        preferences.theme = settings.value(QStringLiteral("theme")).toString();
    else // before the theme menu: a "Dark Theme" checkbox
        preferences.theme = settings.value(QStringLiteral("darkTheme"), true).toBool() ? QStringLiteral("dark")
                                                                                        : QStringLiteral("light");
    preferences.codeFontFamily = settings.value(QStringLiteral("codeFont/family")).toString();
    preferences.codeFontSize = std::clamp(settings.value(QStringLiteral("codeFont/size"), 11).toInt(),
                                          minimumFontSize, maximumFontSize);
    preferences.systemTitleBar = settings.value(QStringLiteral("systemTitleBar"), false).toBool();

    preferences.editor.codeFolding = settings.value(QStringLiteral("editor/codeFolding"), true).toBool();
    preferences.editor.wrapLines = settings.value(QStringLiteral("editor/wrapLines"), false).toBool();
    preferences.editor.showWhitespace = settings.value(QStringLiteral("editor/showWhitespace"), false).toBool();
    preferences.editor.autoCompletion = settings.value(QStringLiteral("editor/autoCompletion"), true).toBool();
    preferences.formatOnSave = settings.value(QStringLiteral("formatOnSave"), false).toBool();
    preferences.languageServers = settings.value(QStringLiteral("editor/languageServers"), true).toBool();

    preferences.alwaysSelectOpenedFile = settings.value(QStringLiteral("alwaysSelectOpenedFile"), false).toBool();

    preferences.snapshotBeforePrompt = settings.value(QStringLiteral("snapshotBeforePrompt"), true).toBool();
    preferences.claudeNotifications = settings.value(QStringLiteral("claudeNotifications"), true).toBool();
    return preferences;
}

void Preferences::save() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("theme"), theme);
    settings.setValue(QStringLiteral("codeFont/family"), codeFontFamily);
    settings.setValue(QStringLiteral("codeFont/size"), codeFontSize);
    settings.setValue(QStringLiteral("systemTitleBar"), systemTitleBar);

    settings.setValue(QStringLiteral("editor/codeFolding"), editor.codeFolding);
    settings.setValue(QStringLiteral("editor/wrapLines"), editor.wrapLines);
    settings.setValue(QStringLiteral("editor/showWhitespace"), editor.showWhitespace);
    settings.setValue(QStringLiteral("editor/autoCompletion"), editor.autoCompletion);
    settings.setValue(QStringLiteral("formatOnSave"), formatOnSave);
    settings.setValue(QStringLiteral("editor/languageServers"), languageServers);

    settings.setValue(QStringLiteral("alwaysSelectOpenedFile"), alwaysSelectOpenedFile);

    settings.setValue(QStringLiteral("snapshotBeforePrompt"), snapshotBeforePrompt);
    settings.setValue(QStringLiteral("claudeNotifications"), claudeNotifications);
}

QFont Preferences::codeFont() const
{
    QFont font = AppFonts::codeFont(codeFontSize);
    if (!codeFontFamily.isEmpty())
        font.setFamily(codeFontFamily);
    return font;
}
