// SPDX-License-Identifier: GPL-3.0-only

#include "PreferencesPopup.h"

#include "TerminalPreferencesPage.h"
#include "ui/AppFonts.h"
#include "ui/Paragraph.h"
#include "ui/PopupWidgets.h"
#include "ui/SettingsSwitch.h"
#include "ui/TreeMetrics.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int commandIndexRole = Qt::UserRole;

QLabel *mutedLabel(const QString &text, const char *objectName = "pageDescription")
{
    auto *label = new QLabel;
    label->setObjectName(QLatin1String(objectName));
    Paragraph::setText(label, text);
    return label;
}

// A heading and its card, as one block of a page.
void addGroup(QVBoxLayout *layout, const QString &heading, QWidget *card)
{
    if (layout->count() > 0)
        layout->addSpacing(22);
    layout->addWidget(sectionHeading(heading));
    layout->addSpacing(8);
    layout->addWidget(card);
}

QString shortcutsText(const QList<QKeySequence> &shortcuts)
{
    QStringList texts;
    for (const QKeySequence &shortcut : shortcuts)
        texts.append(shortcut.toString(QKeySequence::NativeText));
    return texts.join(QStringLiteral(", "));
}

// A font size, or 0 for the default one shown as `defaultText` (one step
// below the smallest size).
QSpinBox *defaultableFontSize(const QString &defaultText)
{
    auto *size = new QSpinBox;
    size->setRange(Preferences::minimumFontSize - 1, Preferences::maximumFontSize);
    size->setSpecialValueText(defaultText);
    size->setSuffix(QCoreApplication::translate("PreferencesPopup", " pt"));
    size->setMinimumWidth(140);
    return size;
}

int fontSizeValue(const QSpinBox *size)
{
    return size->value() == size->minimum() ? 0 : size->value();
}

void setFontSizeValue(QSpinBox *size, int value)
{
    size->setValue(value > 0 ? value : size->minimum());
}

void addDisabledItem(QComboBox *combo, const QString &text)
{
    combo->addItem(text);
    if (const auto *model = qobject_cast<QStandardItemModel *>(combo->model()))
        model->item(combo->count() - 1)->setEnabled(false);
}

// One link per download page: the Monaspace family shares one.
QStringList downloadLinks(const QList<AppFonts::RecommendedFont> &missingFonts)
{
    QList<std::pair<QString, QStringList>> missing;
    for (const AppFonts::RecommendedFont &font : missingFonts) {
        if (missing.isEmpty() || missing.last().first != font.url)
            missing.append({font.url, {}});
        missing.last().second.append(font.name);
    }
    QStringList sources;
    for (const auto &[url, names] : std::as_const(missing)) {
        // "Monaspace" for its five styles.
        const QString name = names.size() > 1 ? names.first().section(QLatin1Char(' '), 0, 0) : names.first();
        sources.append(QStringLiteral("<a href=\"%1\">%2</a>").arg(url, name.toHtmlEscaped()));
    }
    return sources;
}

bool isOtherCodeFamily(const QString &family, const QStringList &recommended)
{
    const bool isUsableMonospace = QFontDatabase::isFixedPitch(family) && !QFontDatabase::isPrivateFamily(family);
    return isUsableMonospace && !AppFonts::isBuiltInCode(family) && !recommended.contains(family);
}

// Fills `combo` with the built-in fonts, the recommended ones (those not
// installed shown but disabled), then the other monospaced fonts
// installed. Returns the download links of the missing recommended fonts.
QStringList fillFontFamilies(QComboBox *combo)
{
    combo->addItem(QCoreApplication::translate("PreferencesPopup", "Droid Sans Mono (built in)"), QString());
    combo->addItem(QCoreApplication::translate("PreferencesPopup", "Ubuntu Sans Mono (built in)"),
                   AppFonts::ubuntuSansMono());
    combo->addItem(QCoreApplication::translate("PreferencesPopup", "JetBrains Mono (built in)"),
                   AppFonts::jetBrainsMono());
    combo->insertSeparator(combo->count());
    QStringList recommended;
    QList<AppFonts::RecommendedFont> missing;
    for (const AppFonts::RecommendedFont &font : AppFonts::recommendedFonts()) {
        const QString family = AppFonts::installedFamily(font);
        if (family.isEmpty()) {
            addDisabledItem(combo,
                            QCoreApplication::translate("PreferencesPopup", "%1 — not installed").arg(font.name));
            missing.append(font);
            continue;
        }
        combo->addItem(font.name, family);
        recommended.append(family);
    }
    combo->insertSeparator(combo->count());
    for (const QString &family : QFontDatabase::families()) {
        if (isOtherCodeFamily(family, recommended))
            combo->addItem(family, family);
    }
    return downloadLinks(missing);
}

} // namespace

QWidget *PreferencesPopup::themeGrid(const QList<Theme> &themes)
{
    constexpr int columns = 3;
    auto *grid = new QWidget;
    auto *layout = new QGridLayout(grid);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    for (int i = 0; i < themes.size(); ++i) {
        auto *card = new ThemeCard(themes[i], grid);
        themes_->addButton(card);
        layout->addWidget(card, i / columns, i % columns);
    }
    // A short last row keeps the cards' width.
    for (int column = 0; column < columns; ++column)
        layout->setColumnStretch(column, 1);
    return grid;
}

QWidget *PreferencesPopup::createAppearancePage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // The wider previews keep each theme name readable beside the navigation.
    themes_ = new QButtonGroup(this);
    themes_->setExclusive(true);
    connect(themes_, &QButtonGroup::buttonToggled, this, [this](QAbstractButton *, bool checked) {
        if (checked)
            updateFontPreview();
    });
    QList<Theme> sortedThemes = Theme::all();
    std::ranges::sort(sortedThemes, [](const Theme &a, const Theme &b) {
        return QString::compare(a.title, b.title, Qt::CaseInsensitive) < 0;
    });
    addGroup(layout, tr("Theme"), themeGrid(sortedThemes));

    systemTitleBar_ = new SettingsSwitch(content);
#ifdef Q_OS_MACOS
    systemTitleBar_->hide(); // macOS keeps its own frame and traffic lights
#else
    auto *windowCard = new SettingsCard(nullptr, SettingsCard::TogglePlacement::AfterTitle);
    windowCard->addRow(tr("System title bar"),
                       tr("The system's window frame instead of Kodamiko's, for tiling window managers."),
                       systemTitleBar_);
    addGroup(layout, tr("Window"), windowCard);
#endif

    auto *motionCard = new SettingsCard(nullptr, SettingsCard::TogglePlacement::AfterTitle);
    animations_ = new SettingsSwitch;
    animations_->setObjectName(QStringLiteral("interfaceAnimations"));
    motionCard->addRow(tr("Interface animations"),
                       tr("Animate menus, popups, hover feedback and diagram transitions. "
                          "Turn off for immediate changes without motion."),
                       animations_);
    addGroup(layout, tr("Motion"), motionCard);
    return page(tr("Appearance"), tr("Colors, window frame and motion."), content);
}

QWidget *PreferencesPopup::createFontsPage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Code font, with a sample in the chosen theme.
    auto *fontCard = new SettingsCard(nullptr, SettingsCard::TogglePlacement::AfterTitle);
    fontFamily_ = new QComboBox;
    fontFamily_->setMinimumWidth(240);
    const QStringList sources = fillFontFamilies(fontFamily_);
    fontSources_ = new QLabel;
    fontSources_->setObjectName(QStringLiteral("settingsDescription"));
    fontSources_->setOpenExternalLinks(true);
    Paragraph::setHtml(fontSources_, tr("Recommended, to install: %1.").arg(sources.join(QStringLiteral(", "))));
    fontSize_ = new QSpinBox;
    fontSize_->setRange(Preferences::minimumFontSize, Preferences::maximumFontSize);
    fontSize_->setSuffix(tr(" pt"));
    fontSize_->setMinimumWidth(90);
    fontWeight_ = new QComboBox;
    fontWeight_->addItem(tr("Light"), int(QFont::Light));
    fontWeight_->addItem(tr("Regular"), int(QFont::Normal));
    fontWeight_->addItem(tr("Medium"), int(QFont::Medium));
    lineHeight_ = new QDoubleSpinBox;
    lineHeight_->setRange(AppFonts::minimumLineHeight, AppFonts::maximumLineHeight);
    lineHeight_->setSingleStep(0.1);
    lineHeight_->setDecimals(2);
    lineHeight_->setMinimumWidth(90);
    ligatures_ = new SettingsSwitch;
    fontPreview_ = new QLabel;
    fontPreview_->setTextFormat(Qt::RichText);
    connect(fontFamily_, &QComboBox::currentIndexChanged, this, &PreferencesPopup::updateFontPreview);
    connect(fontSize_, &QSpinBox::valueChanged, this, &PreferencesPopup::updateFontPreview);
    connect(fontWeight_, &QComboBox::currentIndexChanged, this, &PreferencesPopup::updateFontPreview);
    connect(lineHeight_, &QDoubleSpinBox::valueChanged, this, &PreferencesPopup::updateFontPreview);
    connect(ligatures_, &QAbstractButton::toggled, this, &PreferencesPopup::updateFontPreview);
    fontCard->addRow(tr("Font"), tr("The editor, the terminals and the diffs use it."), fontFamily_);
    fontCard->addWidget(fontSources_);
    // Only once in the card: shown without a parent, the label would be a
    // window of its own, and on macOS leave a full-screen Space for another.
    fontSources_->setVisible(!sources.isEmpty());
    fontCard->addRow(tr("Size"), QString(), fontSize_);
    fontCard->addRow(tr("Weight"), tr("Light reads finer on a bright screen; Medium, bolder."), fontWeight_);
    fontCard->addRow(tr("Line height"), tr("Lines of code, as a multiple of the font size (not in the terminals)."),
                     lineHeight_);
    fontCard->addRow(tr("Ligatures"),
                     tr("In the editor, \"->\", \"!=\" or \"=>\" drawn as one sign, with a font that has "
                        "them (JetBrains Mono). The terminals keep one character per cell."),
                     ligatures_);
    fontCard->addWidget(fontPreview_);
    addGroup(layout, tr("Code font"), fontCard);

    // Interface font: Ubuntu Sans, 11 pt unless another size is chosen.
    auto *interfaceCard = new SettingsCard(nullptr, SettingsCard::TogglePlacement::AfterTitle);
    interfaceFontSize_ = new QSpinBox;
    interfaceFontSize_->setRange(Preferences::minimumFontSize, Preferences::maximumFontSize);
    interfaceFontSize_->setSuffix(tr(" pt"));
    interfaceFontSize_->setMinimumWidth(140);
    treeFontSize_ = defaultableFontSize(tr("Code font size"));
    interfaceCard->addRow(tr("Menus and panels"), tr("Menus, tabs, buttons, panels and dialogs."), interfaceFontSize_);
    interfaceCard->addRow(tr("Navigation trees"), tr("The files, the outline and the changes."), treeFontSize_);
    addGroup(layout, tr("Interface font"), interfaceCard);

    return page(tr("Fonts"), tr("Make code and interface text comfortable to read."), content);
}

QWidget *PreferencesPopup::createEditorPage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *display = new SettingsCard(nullptr, SettingsCard::TogglePlacement::AfterTitle);
    codeFolding_ = new SettingsSwitch;
    wrapLines_ = new SettingsSwitch;
    showWhitespace_ = new SettingsSwitch;
    display->addRow(tr("Code folding"), tr("Marks in the margin fold blocks, comments and #if sections."),
                    codeFolding_);
    display->addRow(tr("Wrap long lines"), tr("Long lines continue below instead of scrolling sideways."), wrapLines_);
    display->addRow(tr("Show whitespace"), tr("Spaces appear as dots and tabs as arrows."), showWhitespace_);
    addGroup(layout, tr("Display"), display);

    auto *completion = new SettingsCard(nullptr, SettingsCard::TogglePlacement::AfterTitle);
    autoCompletion_ = new QComboBox;
    autoCompletion_->addItem(tr("Off"), int(AutoCompletion::Off));
    autoCompletion_->addItem(tr("Calm"), int(AutoCompletion::Calm));
    autoCompletion_->addItem(tr("Eager"), int(AutoCompletion::Eager));
    autoCompletion_->setAccessibleName(tr("Suggest while typing"));
    completion->addRow(tr("Suggest while typing"),
                       tr("Off: only on Ctrl+Space. Calm: after a pause, from the third letter of a name. "
                          "Eager: as soon as you type a name or a member access."),
                       autoCompletion_);
    completionAcceptsEnter_ = new SettingsSwitch;
    completion->addRow(tr("Enter inserts the suggestion"),
                       tr("Otherwise only Tab inserts it, and Enter always starts a new line."),
                       completionAcceptsEnter_);
    languageServers_ = new SettingsSwitch;
    completion->addRow(tr("Language servers"),
                       tr("Members, signatures and types from clangd (C, C++), jdtls (Java), "
                          "typescript-language-server, pyright or pylsp, when installed."),
                       languageServers_);
    serversFound_ = mutedLabel(QString(), "settingsDescription");
    completion->addWidget(serversFound_);
    addGroup(layout, tr("Completion"), completion);

    auto *fullLine = new SettingsCard(nullptr, SettingsCard::TogglePlacement::AfterTitle);
    fullLine_ = new SettingsSwitch;
    fullLine->addRow(tr("Full Line completion"),
                     tr("A code model on this machine suggests the rest of the line in gray; Tab inserts it. "
                        "The code around the cursor is sent to the server below."),
                     fullLine_);
    fullLineServer_ = new QComboBox;
    fullLineServer_->addItem(tr("Ollama"), QStringLiteral("ollama"));
    fullLineServer_->addItem(tr("llama.cpp server"), QStringLiteral("llama.cpp"));
    fullLineServer_->setAccessibleName(tr("Model server"));
    fullLineServerRow_ =
        fullLine->addRow(tr("Server"), tr("Ollama, or llama.cpp's llama-server with a model that fills in the middle."),
                         fullLineServer_);
    fullLineEndpoint_ = new QLineEdit;
    fullLineEndpoint_->setAccessibleName(tr("Server address"));
    fullLineEndpoint_->setMinimumWidth(220);
    fullLineEndpointRow_ = fullLine->addRow(tr("Address"), QString(), fullLineEndpoint_);
    fullLineModel_ = new QLineEdit;
    fullLineModel_->setAccessibleName(tr("Model"));
    fullLineModel_->setMinimumWidth(220);
    fullLineModelRow_ =
        fullLine->addRow(tr("Model"), tr("Ollama's model name, such as qwen2.5-coder:1.5b."), fullLineModel_);
    connect(fullLine_, &QCheckBox::toggled, this, &PreferencesPopup::updateFullLineRows);
    connect(fullLineServer_, &QComboBox::currentIndexChanged, this, [this] {
        // Each server's usual port, unless another was set.
        const QString endpoint = fullLineEndpoint_->text().trimmed();
        const bool ollama = fullLineServer_->currentData().toString() == QLatin1String("ollama");
        if (endpoint.isEmpty() || endpoint == QLatin1String("http://localhost:11434") ||
            endpoint == QLatin1String("http://localhost:8080"))
            fullLineEndpoint_->setText(ollama ? QStringLiteral("http://localhost:11434")
                                              : QStringLiteral("http://localhost:8080"));
        updateFullLineRows();
    });
    addGroup(layout, tr("Full Line"), fullLine);

    auto *saving = new SettingsCard(nullptr, SettingsCard::TogglePlacement::AfterTitle);
    formatOnSave_ = new SettingsSwitch;
    saving->addRow(tr("Format on save"),
                   tr("Formats each file before writing it, with clang-format, prettier, ruff, black, shfmt or "
                      "cmake-format, whichever is installed for its language."),
                   formatOnSave_);
    addGroup(layout, tr("Saving"), saving);

    return page(tr("Editor"), tr("How code is shown and saved."), content);
}

QWidget *PreferencesPopup::createFilesPage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *tree = new SettingsCard(nullptr, SettingsCard::TogglePlacement::AfterTitle);
    alwaysSelectOpenedFile_ = new SettingsSwitch;
    tree->addRow(tr("Always select the opened file"), tr("The Files tree reveals each file you open."),
                 alwaysSelectOpenedFile_);
    addGroup(layout, tr("Files panel"), tree);

    return page(tr("Files"), tr("Choose how the project tree follows your work."), content);
}

QWidget *PreferencesPopup::createClaudePage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *session = new SettingsCard(nullptr, SettingsCard::TogglePlacement::AfterTitle);
    snapshotBeforePrompt_ = new SettingsSwitch;
    shareEditorContext_ = new SettingsSwitch;
    reportEditErrors_ = new SettingsSwitch;
    exposeTools_ = new SettingsSwitch;
    gitThroughKDKTools_ = new SettingsSwitch;
    claudeNotifications_ = new SettingsSwitch;
    session->addRow(tr("Snapshot before each prompt"),
                    tr("Keeps a copy of the working directory before Claude works on a prompt, to undo its "
                       "changes."),
                    snapshotBeforePrompt_);
    session->addRow(tr("Share editor context with each prompt"),
                    tr("Tells Claude the open file, the cursor, the selection, the problems shown and the unsaved "
                       "files, so that \u201cthis\u201d needs no @path."),
                    shareEditorContext_);
    session->addRow(tr("Report errors after each edit"),
                    tr("Tells Claude the errors the language servers find in the code it has just edited, so "
                       "that it fixes them before going on."),
                    reportEditErrors_);
    session->addRow(tr("Expose Kodamiko's tools to Claude"),
                    tr("Lets Claude read and act in the editor (open files with their unsaved changes, problems, "
                       "diagrams, terminals, git) through Kodamiko's MCP server. Applies to the next session."),
                    exposeTools_);
    session->addRow(tr("Send git commands to Kodamiko's tools"),
                    tr("When Claude runs a git command that changes the repository in its shell, Kodamiko refuses it "
                       "and tells Claude which of its git tools does the same: they ask you first and take "
                       "snapshots. Reading git stays allowed. Needs Kodamiko's tools."),
                    gitThroughKDKTools_);
    session->addRow(tr("Notifications"),
                    tr("Alerts you when Claude needs you or has finished while you are elsewhere."),
                    claudeNotifications_);
    addGroup(layout, tr("Session"), session);

    return page(tr("Claude"), tr("Working alongside Claude Code."), content);
}

QWidget *PreferencesPopup::createKeymapPage()
{
    auto *content = new QWidget;

    commandFilter_ = new QLineEdit(content);
    commandFilter_->setObjectName(QStringLiteral("keymapSearch"));
    commandFilter_->setPlaceholderText(tr("Search commands or shortcuts"));
    commandFilter_->setClearButtonEnabled(true);
    commandFilter_->addAction(new QAction(commandFilter_), QLineEdit::LeadingPosition); // icon set by applyTheme
    connect(commandFilter_, &QLineEdit::textChanged, this, &PreferencesPopup::fillCommandList);

    auto *listCard = new QFrame(content);
    listCard->setObjectName(QStringLiteral("settingsCard"));
    commandList_ = new QTreeWidget(listCard);
    commandList_->setObjectName(QStringLiteral("keymapList"));
    commandList_->setColumnCount(2);
    commandList_->setHeaderHidden(true);
    commandList_->setRootIsDecorated(false);
    commandList_->setUniformRowHeights(true);
    applyTreeRowHeight(commandList_);
    commandList_->setItemDelegateForColumn(1, new KeycapDelegate(commandList_));
    commandList_->header()->setStretchLastSection(false);
    commandList_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    commandList_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    commandList_->header()->resizeSection(1, 230);
    connect(commandList_, &QTreeWidget::currentItemChanged, this, &PreferencesPopup::onCommandSelected);
    auto *listLayout = new QVBoxLayout(listCard);
    listLayout->setContentsMargins(4, 4, 4, 4);
    listLayout->addWidget(commandList_);

    // The selected command's shortcut.
    auto *editCard = new QFrame(content);
    editCard->setObjectName(QStringLiteral("settingsCard"));
    selectedTitle_ = new QLabel(editCard);
    AppFonts::setInterfaceFont(selectedTitle_, 1);
    shortcutEdit_ = new QKeySequenceEdit(editCard);
    shortcutEdit_->setMaximumSequenceLength(2);
    shortcutEdit_->setClearButtonEnabled(false);
    shortcutEdit_->setToolTip(tr("Press the new shortcut"));
    shortcutEdit_->setMinimumWidth(200);
    connect(shortcutEdit_, &QKeySequenceEdit::editingFinished, this,
            [this] { assignShortcut(shortcutEdit_->keySequence()); });
    removeShortcut_ = new QPushButton(tr("Remove"), editCard);
    connect(removeShortcut_, &QPushButton::clicked, this, [this] {
        if (const Keymap::Command *command = selectedCommand())
            setShortcuts(*command, {});
        setNote({});
    });
    resetShortcut_ = new QPushButton(tr("Reset"), editCard);
    resetShortcut_->setToolTip(tr("Back to the default shortcut"));
    connect(resetShortcut_, &QPushButton::clicked, this, [this] {
        if (const Keymap::Command *command = selectedCommand())
            setShortcuts(*command, command->defaultShortcuts);
        setNote({});
    });
    auto *resetAll = new QPushButton(tr("Reset All"), editCard);
    resetAll->setToolTip(tr("Every command back to its default shortcut"));
    connect(resetAll, &QPushButton::clicked, this, [this] {
        overrides_.clear();
        setNote({});
        updateCommandRows();
        updateSummary();
    });
    keymapNote_ = mutedLabel(QString(), "keymapNote");
    keymapNote_->hide();

    auto *editRow = new QHBoxLayout;
    editRow->setSpacing(8);
    editRow->addWidget(shortcutEdit_, 1);
    editRow->addWidget(removeShortcut_);
    editRow->addWidget(resetShortcut_);
    editRow->addSpacing(16);
    editRow->addWidget(resetAll);
    auto *editLayout = new QVBoxLayout(editCard);
    editLayout->setContentsMargins(16, 12, 16, 12);
    editLayout->setSpacing(8);
    editLayout->addWidget(selectedTitle_);
    editLayout->addLayout(editRow);
    editLayout->addWidget(keymapNote_);

    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(commandFilter_);
    layout->addWidget(listCard, 1);
    layout->addWidget(editCard);

    return page(tr("Keymap"), tr("Shortcuts of the menu commands. Select one, then press its new keys."), content,
                false);
}

void PreferencesPopup::setInstalledServers(const QStringList &names)
{
    Paragraph::setText(serversFound_, names.isEmpty() ? tr("No language server found on this machine.")
                                                      : tr("Found: %1.").arg(names.join(QStringLiteral(", "))));
}

void PreferencesPopup::updateFontPreview()
{
    if (!fontPreview_)
        return;
    Theme shown = theme_;
    if (const auto *card = qobject_cast<ThemeCard *>(themes_->checkedButton()))
        shown = Theme::named(card->themeName());
    const auto color = [&shown](const char *category) {
        const int index = shown.syntaxStyleIndex(QString::fromLatin1(category));
        return (index >= 0 ? shown.syntaxStyles[index].second.foreground : shown.foreground).name();
    };
    const auto span = [&color](const char *category, const QString &text) {
        return QStringLiteral("<span style=\"color:%1\">%2</span>").arg(color(category), text.toHtmlEscaped());
    };
    const QString html =
        QStringLiteral("<pre style=\"margin:0; line-height:%8%\">%1\n%2 %3 %4 %5(%6, %7);\n%9</pre>")
            .arg(span("comment", QStringLiteral("// The quick brown fox jumps over 0O 1lI {}[]")),
                 span("keyword", QStringLiteral("const auto")), span("variable", QStringLiteral("answer")),
                 span("operator", QStringLiteral("=")), span("function", QStringLiteral("compute")),
                 span("string", QStringLiteral("\"text\"")), span("number", QStringLiteral("42")))
            .arg(qRound(lineHeight_->value() * 100))
            .arg(span("operator", QStringLiteral("if (a != b && c >= d) return x => x->next;")));
    fontPreview_->setText(html);

    Preferences font;
    font.codeFontFamily = fontFamily_->currentData().toString();
    font.codeFontSize = fontSize_->value();
    font.codeFontWeight = fontWeight_->currentData().toInt();
    fontPreview_->setFont(AppFonts::withLigatures(font.codeFont(), ligatures_->isChecked()));
    fontPreview_->setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; border: 1px solid %3; "
                                               "border-radius: 3px; padding: 10px 12px; }")
                                    .arg(shown.background.name(), shown.foreground.name(), shown.line.name()));
}

void PreferencesPopup::popup(const Preferences &preferences,
                             const QList<Keymap::Command> &commands,
                             const Keymap::Overrides &overrides)
{
    load(preferences, commands, overrides);
    showCentered();
    pageList_->setFocus();
}

void PreferencesPopup::load(const Preferences &preferences,
                            const QList<Keymap::Command> &commands,
                            const Keymap::Overrides &overrides)
{
    loading_ = true;
    baseline_ = preferences;
    for (QAbstractButton *button : themes_->buttons()) {
        if (static_cast<ThemeCard *>(button)->themeName() == preferences.theme)
            button->setChecked(true);
    }
    if (!themes_->checkedButton() && !themes_->buttons().isEmpty())
        themes_->buttons().first()->setChecked(true);
    fontFamily_->setCurrentIndex(std::max(fontFamily_->findData(preferences.codeFontFamily), 0));
    fontSize_->setValue(preferences.codeFontSize);
    // A weight set by hand in the settings: the closest offered.
    int weight = 0;
    for (int i = 1; i < fontWeight_->count(); ++i) {
        if (std::abs(fontWeight_->itemData(i).toInt() - preferences.codeFontWeight) <
            std::abs(fontWeight_->itemData(weight).toInt() - preferences.codeFontWeight))
            weight = i;
    }
    fontWeight_->setCurrentIndex(weight);
    lineHeight_->setValue(preferences.codeLineHeight);
    ligatures_->setChecked(preferences.ligatures);
    interfaceFontSize_->setValue(preferences.interfaceFontSize);
    setFontSizeValue(treeFontSize_, preferences.treeFontSize);
    systemTitleBar_->setChecked(preferences.systemTitleBar);
    animations_->setChecked(preferences.animations);
    codeFolding_->setChecked(preferences.editor.codeFolding);
    wrapLines_->setChecked(preferences.editor.wrapLines);
    showWhitespace_->setChecked(preferences.editor.showWhitespace);
    autoCompletion_->setCurrentIndex(std::max(autoCompletion_->findData(int(preferences.editor.autoCompletion)), 0));
    completionAcceptsEnter_->setChecked(preferences.editor.completionAcceptsEnter);
    languageServers_->setChecked(preferences.languageServers);
    fullLine_->setChecked(preferences.fullLineCompletion);
    {
        const QSignalBlocker blocker(fullLineServer_);
        fullLineServer_->setCurrentIndex(std::max(fullLineServer_->findData(preferences.fullLineServer), 0));
    }
    fullLineEndpoint_->setText(preferences.fullLineEndpoint);
    fullLineModel_->setText(preferences.fullLineModel);
    updateFullLineRows();
    formatOnSave_->setChecked(preferences.formatOnSave);
    alwaysSelectOpenedFile_->setChecked(preferences.alwaysSelectOpenedFile);
    snapshotBeforePrompt_->setChecked(preferences.snapshotBeforePrompt);
    shareEditorContext_->setChecked(preferences.shareEditorContext);
    reportEditErrors_->setChecked(preferences.reportEditErrors);
    exposeTools_->setChecked(preferences.exposeTools);
    gitThroughKDKTools_->setChecked(preferences.gitThroughKDKTools);
    claudeNotifications_->setChecked(preferences.claudeNotifications);
    terminalPage_->setPathFolders(preferences.terminalPathFolders);
    terminalPage_->setOptionKey(preferences.terminalOptionKey);

    commands_ = commands;
    overrides_ = overrides;
    setNote({});
    commandFilter_->clear();
    fillCommandList();
    updateFontPreview();
    baseline_ = this->preferences();
    baselineOverrides_ = overrides_;
    loading_ = false;
    updateSummary();
}

void PreferencesPopup::updateFullLineRows()
{
    const bool on = fullLine_->isChecked();
    fullLineServerRow_->setVisible(on);
    fullLineEndpointRow_->setVisible(on);
    fullLineModelRow_->setVisible(on && fullLineServer_->currentData().toString() == QLatin1String("ollama"));
}

void PreferencesPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}

Preferences PreferencesPopup::preferences() const
{
    Preferences preferences = baseline_;
    if (const auto *card = qobject_cast<ThemeCard *>(themes_->checkedButton()))
        preferences.theme = card->themeName();
    preferences.codeFontFamily = fontFamily_->currentData().toString();
    preferences.codeFontSize = fontSize_->value();
    preferences.codeFontWeight = fontWeight_->currentData().toInt();
    preferences.codeLineHeight = lineHeight_->value();
    preferences.ligatures = ligatures_->isChecked();
    preferences.interfaceFontSize = interfaceFontSize_->value();
    preferences.treeFontSize = fontSizeValue(treeFontSize_);
    preferences.systemTitleBar = systemTitleBar_->isChecked();
    preferences.animations = animations_->isChecked();
    preferences.editor.codeFolding = codeFolding_->isChecked();
    preferences.editor.wrapLines = wrapLines_->isChecked();
    preferences.editor.showWhitespace = showWhitespace_->isChecked();
    preferences.editor.autoCompletion = static_cast<AutoCompletion>(autoCompletion_->currentData().toInt());
    preferences.editor.completionAcceptsEnter = completionAcceptsEnter_->isChecked();
    preferences.languageServers = languageServers_->isChecked();
    preferences.fullLineCompletion = fullLine_->isChecked();
    preferences.fullLineServer = fullLineServer_->currentData().toString();
    preferences.fullLineEndpoint = fullLineEndpoint_->text().trimmed();
    preferences.fullLineModel = fullLineModel_->text().trimmed();
    preferences.formatOnSave = formatOnSave_->isChecked();
    preferences.alwaysSelectOpenedFile = alwaysSelectOpenedFile_->isChecked();
    preferences.snapshotBeforePrompt = snapshotBeforePrompt_->isChecked();
    preferences.shareEditorContext = shareEditorContext_->isChecked();
    preferences.reportEditErrors = reportEditErrors_->isChecked();
    preferences.exposeTools = exposeTools_->isChecked();
    preferences.gitThroughKDKTools = gitThroughKDKTools_->isChecked();
    preferences.claudeNotifications = claudeNotifications_->isChecked();
    preferences.terminalPathFolders = terminalPage_->pathFolders();
    preferences.terminalOptionKey = terminalPage_->optionKey();
    return preferences;
}

const Keymap::Command *PreferencesPopup::selectedCommand() const
{
    const QTreeWidgetItem *item = commandList_->currentItem();
    return item ? &commands_[item->data(0, commandIndexRole).toInt()] : nullptr;
}

void PreferencesPopup::selectCommand(const QString &id)
{
    for (int row = 0; row < commandList_->topLevelItemCount(); ++row) {
        QTreeWidgetItem *item = commandList_->topLevelItem(row);
        if (commands_[item->data(0, commandIndexRole).toInt()].id == id)
            commandList_->setCurrentItem(item);
    }
}

void PreferencesPopup::fillCommandList()
{
    const QString filter = commandFilter_->text().trimmed();
    commandList_->clear();
    for (int i = 0; i < commands_.size(); ++i) {
        const Keymap::Command &command = commands_[i];
        const QString shortcuts = shortcutsText(Keymap::shortcutsOf(command, overrides_));
        if (!filter.isEmpty() && !command.title.contains(filter, Qt::CaseInsensitive) &&
            !shortcuts.contains(filter, Qt::CaseInsensitive))
            continue;
        auto *item = new QTreeWidgetItem(commandList_, {command.title, shortcuts});
        item->setData(0, commandIndexRole, i);
    }
    updateCommandRows();
}

void PreferencesPopup::updateCommandRows()
{
    for (int row = 0; row < commandList_->topLevelItemCount(); ++row) {
        QTreeWidgetItem *item = commandList_->topLevelItem(row);
        const Keymap::Command &command = commands_[item->data(0, commandIndexRole).toInt()];
        const QList<QKeySequence> shortcuts = Keymap::shortcutsOf(command, overrides_);
        item->setText(1, shortcutsText(shortcuts));
        item->setData(1, KeycapDelegate::shortcutsRole,
                      QKeySequence::listToString(shortcuts, QKeySequence::PortableText));
        // Changed by the user: in bold.
        QFont font = item->font(0);
        font.setWeight(overrides_.contains(command.id) ? QFont::DemiBold : QFont::Normal);
        item->setFont(0, font);
    }
    onCommandSelected();
}

void PreferencesPopup::onCommandSelected()
{
    const Keymap::Command *command = selectedCommand();
    selectedTitle_->setText(command ? command->title : tr("Select a command"));
    shortcutEdit_->setEnabled(command);
    removeShortcut_->setEnabled(command && !Keymap::shortcutsOf(*command, overrides_).isEmpty());
    resetShortcut_->setEnabled(command && overrides_.contains(command->id));
    const QSignalBlocker blocker(shortcutEdit_);
    shortcutEdit_->setKeySequence(command ? Keymap::shortcutsOf(*command, overrides_).value(0) : QKeySequence());
}

void PreferencesPopup::setShortcuts(const Keymap::Command &command, const QList<QKeySequence> &shortcuts)
{
    if (shortcuts == command.defaultShortcuts)
        overrides_.remove(command.id);
    else
        overrides_.insert(command.id, shortcuts);
    updateCommandRows();
    updateSummary();
}

void PreferencesPopup::setNote(const QString &text)
{
    Paragraph::setText(keymapNote_, text);
    keymapNote_->setVisible(!text.isEmpty());
}

void PreferencesPopup::assignShortcut(const QKeySequence &shortcut)
{
    const Keymap::Command *command = selectedCommand();
    if (!command || shortcut.isEmpty() || Keymap::shortcutsOf(*command, overrides_).value(0) == shortcut)
        return;

    // Qt runs neither of two commands sharing a shortcut: the other one
    // loses it.
    QStringList takenFrom;
    for (const Keymap::Command &other : std::as_const(commands_)) {
        if (other.id == command->id)
            continue;
        QList<QKeySequence> shortcuts = Keymap::shortcutsOf(other, overrides_);
        if (shortcuts.removeAll(shortcut) > 0) {
            takenFrom.append(other.title);
            setShortcuts(other, shortcuts);
        }
    }
    setShortcuts(*command, {shortcut});
    setNote(
        takenFrom.isEmpty()
            ? QString()
            : tr("%1 was removed from %2.").arg(shortcut.toString(QKeySequence::NativeText), takenFrom.join(tr(", "))));
}
