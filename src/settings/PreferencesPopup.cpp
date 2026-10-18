#include "PreferencesPopup.h"

#include "ui/AppFonts.h"
#include "ui/PopupWidgets.h"
#include "ui/UiIcons.h"

#include <QAction>
#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFontDatabase>
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

namespace {

constexpr int commandIndexRole = Qt::UserRole;

QLabel *mutedLabel(const QString &text, const char *objectName = "pageDescription")
{
    auto *label = new QLabel(text);
    label->setObjectName(QLatin1String(objectName));
    label->setWordWrap(true);
    return label;
}

// A heading and its card, as one block of a page.
void addGroup(QVBoxLayout *layout, const QString &heading, QWidget *card)
{
    if (layout->count() > 0)
        layout->addSpacing(18);
    layout->addWidget(sectionHeading(heading));
    layout->addSpacing(6);
    layout->addWidget(card);
}

// The icon in the side bar's colors: muted, and the accent's text color on
// the selected page.
QIcon sidebarIcon(const QString &name, const Theme &theme)
{
    QIcon icon;
    for (const auto &[mode, color] :
         {std::pair{QIcon::Normal, theme.mutedText}, std::pair{QIcon::Selected, theme.accentText}}) {
        const QIcon drawn = UiIcons::icon(name, color);
        for (const QSize size : {QSize(16, 16), QSize(32, 32)})
            icon.addPixmap(drawn.pixmap(size), mode);
    }
    return icon;
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

// The side bar and the keymap, besides the shared rules (popupStyleSheet).
constexpr const char *preferencesRules = R"(
QFrame#preferencesSidebar { background: @window; border: none; border-right: 1px solid @line;
                            border-top-left-radius: 12px; border-bottom-left-radius: 12px; }
QListWidget#preferencesNav { background: transparent; border: none; outline: 0; }
QListWidget#preferencesNav::item { padding: 7px 10px; margin: 1px 0; border-radius: 7px; color: @text; }
QListWidget#preferencesNav::item:hover { background: @hover; }
QListWidget#preferencesNav::item:selected { background: @selection; color: @accentText; }
QLabel#keymapNote { color: @accent; }
QTreeWidget#keymapList { background: transparent; border: none; border-radius: 10px; }
QTreeWidget#keymapList::item { padding: 0 6px; }
QLineEdit#keymapSearch { padding: 6px 10px; border-radius: 8px; }
QKeySequenceEdit QLineEdit { padding: 4px 8px; }
)";

} // namespace

PreferencesPopup::PreferencesPopup(const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("preferences"), QSizeF(0.6, 0.76), QSize(860, 560), parent)
    , theme_(theme)
{
    pages_ = new QStackedWidget(this);
    QWidget *sidebar = createSidebar();
    addPage(QStringLiteral("appearance"), tr("Appearance"), createAppearancePage());
    addPage(QStringLiteral("code"), tr("Editor"), createEditorPage());
    addPage(QStringLiteral("files"), tr("Files"), createFilesPage());
    addPage(QStringLiteral("claude"), tr("Claude"), createClaudePage());
    addPage(QStringLiteral("keyboard"), tr("Keymap"), createKeymapPage());
    pageList_->setCurrentRow(0);

    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("popupFooter"));
    auto *hint = mutedLabel(tr("Changes apply with OK or Apply · Esc to close"), "footerHint");
    auto *cancel = new QPushButton(tr("Cancel"), footer);
    auto *apply = new QPushButton(tr("Apply"), footer);
    auto *ok = new QPushButton(tr("OK"), footer);
    ok->setDefault(true);
    for (QPushButton *button : {cancel, apply, ok})
        button->setMinimumWidth(84);
    connect(cancel, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    connect(apply, &QPushButton::clicked, this, &PreferencesPopup::applied);
    connect(ok, &QPushButton::clicked, this, [this] {
        emit applied();
        dismiss();
    });
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(28, 12, 18, 14);
    footerLayout->setSpacing(8);
    footerLayout->addWidget(hint, 1);
    footerLayout->addWidget(cancel);
    footerLayout->addWidget(apply);
    footerLayout->addWidget(ok);

    auto *main = new QVBoxLayout;
    main->setContentsMargins(0, 0, 0, 0);
    main->setSpacing(0);
    main->addWidget(pages_, 1);
    main->addWidget(footer);

    auto *layout = new QHBoxLayout(this);
    // 1 pixel: the panel's border stays visible around the side bar.
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(sidebar);
    layout->addLayout(main, 1);

    applyTheme(theme);
}

QWidget *PreferencesPopup::createSidebar()
{
    auto *sidebar = new QFrame(this);
    sidebar->setObjectName(QStringLiteral("preferencesSidebar"));
    sidebar->setFixedWidth(210);

    auto *title = new QLabel(tr("Preferences"), sidebar);
    AppFonts::setInterfaceFont(title, 1.15);
    auto *icon = new QLabel(sidebar);
    icon->setObjectName(QStringLiteral("preferencesIcon"));
    auto *header = new QHBoxLayout;
    header->setSpacing(8);
    header->addWidget(icon);
    header->addWidget(title, 1);

    pageList_ = new QListWidget(sidebar);
    pageList_->setObjectName(QStringLiteral("preferencesNav"));
    pageList_->setIconSize(QSize(16, 16));
    pageList_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pageList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(pageList_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(12, 18, 12, 12);
    layout->setSpacing(14);
    layout->addLayout(header);
    layout->addWidget(pageList_, 1);
    return sidebar;
}

void PreferencesPopup::addPage(const QString &icon, const QString &title, QWidget *page)
{
    auto *item = new QListWidgetItem(title, pageList_);
    item->setSizeHint(QSize(0, 34));
    pageIcons_.append(icon);
    pages_->addWidget(page);
}

QWidget *PreferencesPopup::page(const QString &title, const QString &description, QWidget *content, bool scrolls)
{
    auto *page = new QWidget;
    auto *titleLabel = new QLabel(title, page);
    AppFonts::setInterfaceFont(titleLabel, 1.5);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 22, 0, 0);
    layout->setSpacing(0);
    auto *heading = new QVBoxLayout;
    heading->setContentsMargins(28, 0, 28, 0);
    heading->setSpacing(4);
    heading->addWidget(titleLabel);
    heading->addWidget(mutedLabel(description));
    layout->addLayout(heading);
    layout->addSpacing(18);

    content->setObjectName(QStringLiteral("pageContent"));
    if (!scrolls) {
        content->setContentsMargins(28, 0, 28, 18);
        layout->addWidget(content, 1);
        return page;
    }
    // The cards scroll; the title stays.
    auto *holder = new QWidget;
    holder->setObjectName(QStringLiteral("pageContent"));
    auto *holderLayout = new QVBoxLayout(holder);
    holderLayout->setContentsMargins(28, 0, 28, 22);
    holderLayout->addWidget(content);
    holderLayout->addStretch();
    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("pageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(holder);
    scroll->viewport()->setAutoFillBackground(false);
    layout->addWidget(scroll, 1);
    return page;
}

QWidget *PreferencesPopup::createAppearancePage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Theme: one miniature per theme.
    auto *themeRow = new QWidget;
    auto *themeLayout = new QHBoxLayout(themeRow);
    themeLayout->setContentsMargins(0, 0, 0, 0);
    themeLayout->setSpacing(12);
    themes_ = new QButtonGroup(this);
    themes_->setExclusive(true);
    for (const Theme &theme : Theme::all()) {
        auto *card = new ThemeCard(theme, themeRow);
        themes_->addButton(card);
        themeLayout->addWidget(card);
    }
    themeLayout->addStretch();
    connect(themes_, &QButtonGroup::buttonToggled, this, [this](QAbstractButton *, bool checked) {
        if (checked)
            updateFontPreview();
    });
    addGroup(layout, tr("Theme"), themeRow);

    // Code font, with a sample in the chosen theme.
    auto *fontCard = new SettingsCard;
    // The built-in font, the recommended ones (those not installed shown
    // but disabled, with where to get them), then the other monospaced
    // fonts installed.
    fontFamily_ = new QComboBox;
    fontFamily_->setMinimumWidth(240);
    fontFamily_->addItem(tr("JetBrains Mono (built in)"), QString());
    fontFamily_->insertSeparator(fontFamily_->count());
    QStringList recommended;
    // Missing fonts by download page: the Monaspace family shares one.
    QList<std::pair<QString, QStringList>> missing;
    for (const AppFonts::RecommendedFont &font : AppFonts::recommendedFonts()) {
        const QString family = AppFonts::installedFamily(font);
        if (!family.isEmpty()) {
            fontFamily_->addItem(font.name, family);
            recommended.append(family);
            continue;
        }
        fontFamily_->addItem(tr("%1 — not installed").arg(font.name));
        if (const auto *model = qobject_cast<QStandardItemModel *>(fontFamily_->model()))
            model->item(fontFamily_->count() - 1)->setEnabled(false);
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
    fontFamily_->insertSeparator(fontFamily_->count());
    for (const QString &family : QFontDatabase::families()) {
        if (QFontDatabase::isFixedPitch(family) && !QFontDatabase::isPrivateFamily(family) &&
            !AppFonts::isBuiltInCode(family) && !recommended.contains(family))
            fontFamily_->addItem(family, family);
    }
    fontSources_ = new QLabel;
    fontSources_->setObjectName(QStringLiteral("settingsDescription"));
    fontSources_->setWordWrap(true);
    fontSources_->setOpenExternalLinks(true);
    fontSources_->setText(tr("Recommended, to install: %1.").arg(sources.join(QStringLiteral(", "))));
    fontSources_->setVisible(!sources.isEmpty());
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
    ligatures_ = new ToggleSwitch;
    fontPreview_ = new QLabel;
    fontPreview_->setTextFormat(Qt::RichText);
    connect(fontFamily_, &QComboBox::currentIndexChanged, this, &PreferencesPopup::updateFontPreview);
    connect(fontSize_, &QSpinBox::valueChanged, this, &PreferencesPopup::updateFontPreview);
    connect(fontWeight_, &QComboBox::currentIndexChanged, this, &PreferencesPopup::updateFontPreview);
    connect(lineHeight_, &QDoubleSpinBox::valueChanged, this, &PreferencesPopup::updateFontPreview);
    connect(ligatures_, &QAbstractButton::toggled, this, &PreferencesPopup::updateFontPreview);
    fontCard->addRow(tr("Font"), tr("The editor, the terminals and the diffs use it."), fontFamily_);
    fontCard->addWidget(fontSources_);
    fontCard->addRow(tr("Size"), QString(), fontSize_);
    fontCard->addRow(tr("Weight"), tr("Light reads finer on a bright screen; Medium, bolder."), fontWeight_);
    fontCard->addRow(tr("Line height"), tr("Lines of code, as a multiple of the font size (not in the terminals)."),
                     lineHeight_);
    fontCard->addRow(tr("Ligatures"),
                     tr("In the editor, \"->\", \"!=\" or \"=>\" drawn as one sign. The terminals keep one "
                        "character per cell."),
                     ligatures_);
    fontCard->addWidget(fontPreview_);
    addGroup(layout, tr("Code font"), fontCard);

    // Interface font: Inter, at the system's size unless one is chosen.
    auto *interfaceCard = new SettingsCard;
    interfaceFontSize_ = defaultableFontSize(tr("System"));
    treeFontSize_ = defaultableFontSize(tr("Code font size"));
    interfaceCard->addRow(tr("Menus and panels"), tr("Menus, tabs, buttons, panels and dialogs."), interfaceFontSize_);
    interfaceCard->addRow(tr("Navigation trees"), tr("The files, the outline and the changes."), treeFontSize_);
    addGroup(layout, tr("Interface font"), interfaceCard);

    systemTitleBar_ = new ToggleSwitch(content);
#ifdef Q_OS_MACOS
    systemTitleBar_->hide(); // macOS keeps its own frame and traffic lights
#else
    auto *windowCard = new SettingsCard;
    windowCard->addRow(tr("System title bar"),
                       tr("The system's window frame instead of Duet's, for tiling window managers."), systemTitleBar_);
    addGroup(layout, tr("Window"), windowCard);
#endif

    return page(tr("Appearance"), tr("Colors, fonts and window frame."), content);
}

QWidget *PreferencesPopup::createEditorPage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *display = new SettingsCard;
    codeFolding_ = new ToggleSwitch;
    wrapLines_ = new ToggleSwitch;
    showWhitespace_ = new ToggleSwitch;
    display->addRow(tr("Code folding"), tr("Marks in the margin fold blocks, comments and #if sections."),
                    codeFolding_);
    display->addRow(tr("Wrap long lines"), tr("Long lines continue below instead of scrolling sideways."), wrapLines_);
    display->addRow(tr("Show whitespace"), tr("Spaces appear as dots and tabs as arrows."), showWhitespace_);
    addGroup(layout, tr("Display"), display);

    auto *completion = new SettingsCard;
    autoCompletion_ = new QComboBox;
    autoCompletion_->addItem(tr("Off"), int(AutoCompletion::Off));
    autoCompletion_->addItem(tr("Calm"), int(AutoCompletion::Calm));
    autoCompletion_->addItem(tr("Eager"), int(AutoCompletion::Eager));
    autoCompletion_->setAccessibleName(tr("Suggest while typing"));
    completion->addRow(tr("Suggest while typing"),
                       tr("Off: only on Ctrl+Space. Calm: after a pause, from the third letter of a name. "
                          "Eager: as soon as you type a name or a member access."),
                       autoCompletion_);
    completionAcceptsEnter_ = new ToggleSwitch;
    completion->addRow(tr("Enter inserts the suggestion"),
                       tr("Otherwise only Tab inserts it, and Enter always starts a new line."),
                       completionAcceptsEnter_);
    languageServers_ = new ToggleSwitch;
    completion->addRow(tr("Language servers"),
                       tr("Members, signatures and types from clangd (C, C++), jdtls (Java), "
                          "typescript-language-server, pyright or pylsp, when installed."),
                       languageServers_);
    serversFound_ = mutedLabel(QString(), "settingsDescription");
    completion->addWidget(serversFound_);
    addGroup(layout, tr("Completion"), completion);

    auto *fullLine = new SettingsCard;
    fullLine_ = new ToggleSwitch;
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
    connect(fullLine_, &ToggleSwitch::toggled, this, &PreferencesPopup::updateFullLineRows);
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

    auto *saving = new SettingsCard;
    formatOnSave_ = new ToggleSwitch;
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

    auto *tree = new SettingsCard;
    alwaysSelectOpenedFile_ = new ToggleSwitch;
    tree->addRow(tr("Always select the opened file"), tr("The Files tree reveals each file you open."),
                 alwaysSelectOpenedFile_);
    addGroup(layout, tr("Files panel"), tree);

    return page(tr("Files"), tr("The project tree."), content);
}

QWidget *PreferencesPopup::createClaudePage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *session = new SettingsCard;
    snapshotBeforePrompt_ = new ToggleSwitch;
    shareEditorContext_ = new ToggleSwitch;
    exposeTools_ = new ToggleSwitch;
    claudeNotifications_ = new ToggleSwitch;
    session->addRow(tr("Snapshot before each prompt"),
                    tr("Keeps a copy of the working directory before Claude works on a prompt, to undo its "
                       "changes."),
                    snapshotBeforePrompt_);
    session->addRow(tr("Share editor context with each prompt"),
                    tr("Tells Claude the open file, the cursor, the selection, the problems shown and the unsaved "
                       "files, so that \u201cthis\u201d needs no @path."),
                    shareEditorContext_);
    session->addRow(tr("Expose Duet's tools to Claude"),
                    tr("Lets Claude read and act in the editor (open files with their unsaved changes, problems, "
                       "diagrams, terminals, git) through Duet's MCP server. Applies to the next session."),
                    exposeTools_);
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

void PreferencesPopup::applyTheme(const Theme &theme)
{
    theme_ = theme;
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(preferencesRules)));
    if (auto *icon = findChild<QLabel *>(QStringLiteral("preferencesIcon")))
        icon->setPixmap(UiIcons::icon(QStringLiteral("settings"), theme.accent).pixmap(QSize(18, 18)));
    for (int i = 0; i < pageList_->count(); ++i)
        pageList_->item(i)->setIcon(sidebarIcon(pageIcons_.value(i), theme));
    if (!commandFilter_->actions().isEmpty())
        commandFilter_->actions().first()->setIcon(UiIcons::icon(QStringLiteral("search"), theme.mutedText));
    updateFontPreview();
}

void PreferencesPopup::setInstalledServers(const QStringList &names)
{
    serversFound_->setText(names.isEmpty() ? tr("No language server found on this machine.")
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
                                               "border-radius: 8px; padding: 12px 14px; }")
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
    setFontSizeValue(interfaceFontSize_, preferences.interfaceFontSize);
    setFontSizeValue(treeFontSize_, preferences.treeFontSize);
    systemTitleBar_->setChecked(preferences.systemTitleBar);
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
    exposeTools_->setChecked(preferences.exposeTools);
    claudeNotifications_->setChecked(preferences.claudeNotifications);

    commands_ = commands;
    overrides_ = overrides;
    setNote({});
    commandFilter_->clear();
    fillCommandList();
    updateFontPreview();
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
    Preferences preferences;
    if (const auto *card = qobject_cast<ThemeCard *>(themes_->checkedButton()))
        preferences.theme = card->themeName();
    preferences.codeFontFamily = fontFamily_->currentData().toString();
    preferences.codeFontSize = fontSize_->value();
    preferences.codeFontWeight = fontWeight_->currentData().toInt();
    preferences.codeLineHeight = lineHeight_->value();
    preferences.ligatures = ligatures_->isChecked();
    preferences.interfaceFontSize = fontSizeValue(interfaceFontSize_);
    preferences.treeFontSize = fontSizeValue(treeFontSize_);
    preferences.systemTitleBar = systemTitleBar_->isChecked();
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
    preferences.exposeTools = exposeTools_->isChecked();
    preferences.claudeNotifications = claudeNotifications_->isChecked();
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
}

void PreferencesPopup::setNote(const QString &text)
{
    keymapNote_->setText(text);
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
