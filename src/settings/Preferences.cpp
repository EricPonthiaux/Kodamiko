// SPDX-License-Identifier: GPL-3.0-only

#include "Preferences.h"

#include "ui/AppFonts.h"
#include "ui/Theme.h"

#include <QSettings>

#include <algorithm>

Preferences Preferences::load()
{
    const QSettings settings;
    Preferences preferences;
    if (settings.contains(QStringLiteral("theme")))
        preferences.theme = settings.value(QStringLiteral("theme")).toString();
    else if (settings.contains(QStringLiteral("darkTheme"))) // before the theme menu: a "Dark Theme" checkbox
        preferences.theme = QStringLiteral("duet");          // the light themes are gone
    preferences.codeFontFamily = settings.value(QStringLiteral("codeFont/family")).toString();
    preferences.codeFontSize =
        std::clamp(settings.value(QStringLiteral("codeFont/size"), 11).toInt(), minimumFontSize, maximumFontSize);
    preferences.codeFontWeight =
        std::clamp(settings.value(QStringLiteral("codeFont/weight"), int(QFont::Normal)).toInt(), int(QFont::Thin),
                   int(QFont::Black));
    preferences.codeLineHeight =
        std::clamp(settings.value(QStringLiteral("codeFont/lineHeight"), AppFonts::defaultLineHeight).toDouble(),
                   AppFonts::minimumLineHeight, AppFonts::maximumLineHeight);
    preferences.ligatures = settings.value(QStringLiteral("codeFont/ligatures"), false).toBool();
    // 0 (the default size) or a size in the range.
    const auto fontSize = [&settings](const QString &key) {
        const int size = settings.value(key, 0).toInt();
        return size <= 0 ? 0 : std::clamp(size, minimumFontSize, maximumFontSize);
    };
    // 0 before 11 was the default: the system's size.
    const int interfaceFontSize = fontSize(QStringLiteral("interfaceFont/size"));
    preferences.interfaceFontSize = interfaceFontSize > 0 ? interfaceFontSize : AppFonts::defaultInterfaceSize;
    preferences.treeFontSize = fontSize(QStringLiteral("treeFont/size"));
    preferences.systemTitleBar = settings.value(QStringLiteral("systemTitleBar"), false).toBool();
    preferences.animations = settings.value(QStringLiteral("interface/animations"), true).toBool();

    preferences.editor.codeFolding = settings.value(QStringLiteral("editor/codeFolding"), true).toBool();
    preferences.editor.wrapLines = settings.value(QStringLiteral("editor/wrapLines"), false).toBool();
    preferences.editor.showWhitespace = settings.value(QStringLiteral("editor/showWhitespace"), false).toBool();
    // Before the three modes, a switch: on was today's Eager.
    const int legacyMode = settings.value(QStringLiteral("editor/autoCompletion"), true).toBool()
                               ? int(AutoCompletion::Eager)
                               : int(AutoCompletion::Off);
    preferences.editor.autoCompletion = static_cast<AutoCompletion>(
        std::clamp(settings.value(QStringLiteral("editor/completionMode"), legacyMode).toInt(),
                   int(AutoCompletion::Off), int(AutoCompletion::Eager)));
    preferences.editor.completionAcceptsEnter =
        settings.value(QStringLiteral("editor/completionAcceptsEnter"), true).toBool();
    preferences.formatOnSave = settings.value(QStringLiteral("formatOnSave"), true).toBool();
    preferences.languageServers = settings.value(QStringLiteral("editor/languageServers"), true).toBool();
    preferences.fullLineCompletion = settings.value(QStringLiteral("editor/fullLine"), false).toBool();
    preferences.fullLineServer =
        settings.value(QStringLiteral("editor/fullLineServer"), preferences.fullLineServer).toString();
    preferences.fullLineEndpoint =
        settings.value(QStringLiteral("editor/fullLineEndpoint"), preferences.fullLineEndpoint).toString();
    preferences.fullLineModel =
        settings.value(QStringLiteral("editor/fullLineModel"), preferences.fullLineModel).toString();

    preferences.alwaysSelectOpenedFile = settings.value(QStringLiteral("alwaysSelectOpenedFile"), false).toBool();

    preferences.snapshotBeforePrompt = settings.value(QStringLiteral("snapshotBeforePrompt"), true).toBool();
    preferences.shareEditorContext = settings.value(QStringLiteral("shareEditorContext"), true).toBool();
    preferences.reportEditErrors = settings.value(QStringLiteral("reportEditErrors"), true).toBool();
    preferences.defaultAgentProvider =
        settings.value(QStringLiteral("defaultAgentProvider"), preferences.defaultAgentProvider).toString();
    preferences.exposeTools = settings.value(QStringLiteral("exposeTools"), true).toBool();
    preferences.claudeNotifications = settings.value(QStringLiteral("claudeNotifications"), true).toBool();

    preferences.terminalPathFolders = settings.value(QStringLiteral("terminal/pathFolders")).toStringList();
    const bool optionIsMeta = settings.value(QStringLiteral("terminal/optionIsMeta"), false).toBool();
    preferences.terminalOptionKey = optionIsMeta ? OptionKey::Meta : OptionKey::Compose;
    return preferences;
}

void Preferences::save() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("theme"), theme);
    settings.setValue(QStringLiteral("codeFont/family"), codeFontFamily);
    settings.setValue(QStringLiteral("codeFont/size"), codeFontSize);
    settings.setValue(QStringLiteral("codeFont/weight"), codeFontWeight);
    settings.setValue(QStringLiteral("codeFont/lineHeight"), codeLineHeight);
    settings.setValue(QStringLiteral("codeFont/ligatures"), ligatures);
    settings.setValue(QStringLiteral("interfaceFont/size"), interfaceFontSize);
    settings.setValue(QStringLiteral("treeFont/size"), treeFontSize);
    settings.setValue(QStringLiteral("systemTitleBar"), systemTitleBar);
    settings.setValue(QStringLiteral("interface/animations"), animations);

    settings.setValue(QStringLiteral("editor/codeFolding"), editor.codeFolding);
    settings.setValue(QStringLiteral("editor/wrapLines"), editor.wrapLines);
    settings.setValue(QStringLiteral("editor/showWhitespace"), editor.showWhitespace);
    settings.setValue(QStringLiteral("editor/completionMode"), int(editor.autoCompletion));
    settings.setValue(QStringLiteral("editor/completionAcceptsEnter"), editor.completionAcceptsEnter);
    settings.setValue(QStringLiteral("formatOnSave"), formatOnSave);
    settings.setValue(QStringLiteral("editor/languageServers"), languageServers);
    settings.setValue(QStringLiteral("editor/fullLine"), fullLineCompletion);
    settings.setValue(QStringLiteral("editor/fullLineServer"), fullLineServer);
    settings.setValue(QStringLiteral("editor/fullLineEndpoint"), fullLineEndpoint);
    settings.setValue(QStringLiteral("editor/fullLineModel"), fullLineModel);

    settings.setValue(QStringLiteral("alwaysSelectOpenedFile"), alwaysSelectOpenedFile);

    settings.setValue(QStringLiteral("snapshotBeforePrompt"), snapshotBeforePrompt);
    settings.setValue(QStringLiteral("shareEditorContext"), shareEditorContext);
    settings.setValue(QStringLiteral("reportEditErrors"), reportEditErrors);
    settings.setValue(QStringLiteral("defaultAgentProvider"), defaultAgentProvider);
    settings.setValue(QStringLiteral("exposeTools"), exposeTools);
    settings.setValue(QStringLiteral("claudeNotifications"), claudeNotifications);

    settings.setValue(QStringLiteral("terminal/pathFolders"), terminalPathFolders);
    settings.setValue(QStringLiteral("terminal/optionIsMeta"), terminalOptionKey == OptionKey::Meta);
}

QFont Preferences::codeFont() const
{
    QFont font = AppFonts::codeFont(AppFonts::systemPoints(codeFontSize));
    if (!codeFontFamily.isEmpty())
        font.setFamily(codeFontFamily);
    font.setWeight(QFont::Weight(codeFontWeight));
    // The terminal keeps each character in its cell.
    return AppFonts::withLigatures(font, false);
}

void Preferences::applyTypography(Theme &target) const
{
    target.font = codeFont();
    target.lineHeight = codeLineHeight;
    target.editorLigatures = ligatures;
}

QFont Preferences::interfaceFont() const
{
    QFont font = AppFonts::interfaceFont();
    font.setPointSizeF(AppFonts::systemPoints(interfaceFontSize));
    return font;
}

QFont Preferences::treeFont() const
{
    QFont font = interfaceFont();
    if (treeFontSize > 0)
        font.setPointSizeF(AppFonts::systemPoints(treeFontSize));
    else
        font.setPointSizeF(std::max(font.pointSizeF(), AppFonts::systemPoints(codeFontSize)));
    return font;
}
