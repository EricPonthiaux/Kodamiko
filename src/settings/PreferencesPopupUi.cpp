// SPDX-License-Identifier: GPL-3.0-only

#include "PreferencesPopup.h"
#include "TerminalPreferencesPage.h"
#include "ui/AppFonts.h"
#include "ui/Paragraph.h"
#include "ui/PopupFooter.h"
#include "ui/PopupWidgets.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

namespace {

constexpr qreal navigationTitleScale = 0.95;

constexpr auto preferencesRules = R"(
QLabel#preferencePageIcon { background: @slate; border: 1px solid @line; border-radius: 8px; }
QLabel#preferenceNavIcon { background: @panel; border: 1px solid @line; border-radius: 5px; }
QLabel#preferenceNavDescription, QLabel#preferenceScope { color: @muted; }
QListWidget#popupNav::item { padding: 0; margin: 2px 0; border-radius: 6px; }
QFrame#settingsCard { background: @window; border: 1px solid @line; border-radius: 7px; }
QLabel#settingsSection { border: none; padding: 0; }
QComboBox { padding: 6px 10px; border-radius: 5px; }
QLineEdit#keymapSearch { padding: 8px 10px; border-radius: 5px; }
QLabel#keymapNote { color: @accentText; }
QTreeWidget#keymapList::item { padding: 0 6px; }
QKeySequenceEdit QLineEdit { padding: 4px 8px; }
QLabel#footerHint[pending="true"] { color: @accentText; }
)";

QLabel *descriptionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("pageDescription"));
    Paragraph::setText(label, text);
    return label;
}

QString navigationDescription(const QString &icon)
{
    if (icon == QLatin1String("appearance"))
        return QObject::tr("Colors & motion");
    if (icon == QLatin1String("fonts"))
        return QObject::tr("Code & interface text");
    if (icon == QLatin1String("code"))
        return QObject::tr("Writing & completion");
    if (icon == QLatin1String("files"))
        return QObject::tr("Project navigation");
    if (icon == QLatin1String("claude"))
        return QObject::tr("Context & collaboration");
    if (icon == QLatin1String("terminal"))
        return QObject::tr("Environment & keyboard");
    return QObject::tr("Keyboard shortcuts");
}

// The item retains its text for accessibility; the row supplies the two lines.
class NavigationDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        option->text.clear();
    }
};

QWidget *navigationRow(const QString &icon, const QString &title, QWidget *parent)
{
    auto *row = new QWidget(parent);
    row->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *symbol = new QLabel(row);
    symbol->setObjectName(QStringLiteral("preferenceNavIcon"));
    symbol->setProperty("preferenceIcon", icon);
    symbol->setAlignment(Qt::AlignCenter);
    UiMetrics::setScaledSize(symbol, QSize(32, 32));
    auto *name = new QLabel(title, row);
    name->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    AppFonts::setInterfaceFont(name, navigationTitleScale, QFont::DemiBold);
    auto *description = new QLabel(navigationDescription(icon), row);
    description->setObjectName(QStringLiteral("preferenceNavDescription"));
    AppFonts::setInterfaceFont(description, 0.8, QFont::Normal);
    auto *text = new QVBoxLayout;
    text->setAlignment(Qt::AlignTop);
    text->setSpacing(4);
    text->addWidget(name);
    text->addWidget(description);
    auto *iconColumn = new QVBoxLayout;
    iconColumn->addWidget(symbol);
    iconColumn->addStretch();
    AppFonts::followApplicationFont(row, [iconColumn] {
        // Use the application font: the name's font follower may run after this one.
        QFont font = QApplication::font();
        font.setPointSizeF(font.pointSizeF() * navigationTitleScale);
        font.setWeight(QFont::DemiBold);
        const QFontMetrics metrics(font);
        iconColumn->setContentsMargins(0, metrics.ascent() - metrics.capHeight(), 0, 0);
    });
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 10, 8, 10);
    layout->setSpacing(10);
    layout->addLayout(iconColumn);
    layout->addLayout(text, 1);
    return row;
}

} // namespace

PreferencesPopup::PreferencesPopup(const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("preferences"), QSizeF(0.7, 0.86), QSize(960, 680), parent)
    , theme_(theme)
{
    pages_ = new QStackedWidget(this);
    QWidget *sidebar = createSidebar();
    addPage(QStringLiteral("appearance"), tr("Appearance"), createAppearancePage());
    addPage(QStringLiteral("fonts"), tr("Fonts"), createFontsPage());
    addPage(QStringLiteral("code"), tr("Editor"), createEditorPage());
    addPage(QStringLiteral("files"), tr("Files"), createFilesPage());
    addPage(QStringLiteral("claude"), tr("Claude"), createClaudePage());
    terminalPage_ = new TerminalPreferencesPage;
    addPage(QStringLiteral("terminal"), tr("Terminal"),
            page(tr("Terminal"), tr("The environment of the programs Kodamiko launches."), terminalPage_));
    addPage(QStringLiteral("keyboard"), tr("Keymap"), createKeymapPage());
    pageList_->setCurrentRow(0);

    header_ = new PopupHeader(QStringLiteral("settings"), tr("Application Settings"), this);
    header_->setSubtitle(tr("Appearance, editing and everyday tools."));
    auto *scope = new QLabel(tr("All projects"), header_);
    scope->setObjectName(QStringLiteral("preferenceScope"));
    header_->addButton(scope);
    connect(header_, &PopupHeader::closeRequested, this, &OverlayPopup::dismiss);
    auto *columns = new QHBoxLayout;
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(0);
    columns->addWidget(sidebar);
    columns->addWidget(pages_, 1);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addLayout(columns, 1);
    layout->addWidget(createFooter());
    for (const auto *card : findChildren<SettingsCard *>())
        card->layout()->setContentsMargins(16, 4, 16, 4);
    watchChanges();
    baseline_ = preferences();
    updateSummary();
    applyTheme(theme);
}

QWidget *PreferencesPopup::createSidebar()
{
    auto *sidebar = new QFrame(this);
    sidebar->setObjectName(QStringLiteral("popupSidebar"));
    AppFonts::followApplicationFont(sidebar, [sidebar] { sidebar->setFixedWidth(UiMetrics::scaled(248)); });
    auto *title = new QLabel(tr("PREFERENCES"), sidebar);
    title->setObjectName(QStringLiteral("navHeading"));
    AppFonts::setInterfaceFont(title, 0.8, QFont::DemiBold, 0.6);
    pageList_ = new QListWidget(sidebar);
    pageList_->setObjectName(QStringLiteral("popupNav"));
    pageList_->setAccessibleName(tr("Application settings categories"));
    pageList_->setItemDelegate(new NavigationDelegate(pageList_));
    pageList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(pageList_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(12, 18, 12, 16);
    layout->setSpacing(12);
    layout->addWidget(title);
    layout->addWidget(pageList_, 1);
    layout->addWidget(descriptionLabel(tr("Your preferences.\nShared across all projects."), sidebar));
    return sidebar;
}

QWidget *PreferencesPopup::createFooter()
{
    auto *footer = new PopupFooter(this);
    summary_ = footer->hint();
    auto *cancel = new QPushButton(tr("Cancel"), footer);
    apply_ = new QPushButton(tr("Apply"), footer);
    apply_->setObjectName(QStringLiteral("applyPreferences"));
    auto *save = new QPushButton(tr("Save && close"), footer);
    save->setDefault(true);
    for (QPushButton *button : {cancel, apply_, save})
        footer->addButton(button);
    connect(cancel, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    connect(apply_, &QPushButton::clicked, this, &PreferencesPopup::applyChanges);
    connect(save, &QPushButton::clicked, this, [this] {
        applyChanges();
        dismiss();
    });
    return footer;
}

void PreferencesPopup::addPage(const QString &icon, const QString &title, QWidget *content)
{
    auto *item = new QListWidgetItem(title, pageList_);
    QWidget *row = navigationRow(icon, title, pageList_);
    pageList_->setItemWidget(item, row);
    AppFonts::followApplicationFont(row, [item] { item->setSizeHint(QSize(0, UiMetrics::scaled(64))); });
    content->findChild<QLabel *>(QStringLiteral("preferencePageIcon"))->setProperty("preferenceIcon", icon);
    pages_->addWidget(content);
}

QWidget *PreferencesPopup::page(const QString &title, const QString &description, QWidget *content, bool scrolls)
{
    auto *page = new QWidget;
    auto *symbol = new QLabel(page);
    symbol->setObjectName(QStringLiteral("preferencePageIcon"));
    symbol->setAlignment(Qt::AlignCenter);
    UiMetrics::setScaledSize(symbol, QSize(46, 46));
    auto *titleLabel = new QLabel(title, page);
    AppFonts::setInterfaceFont(titleLabel, 1.4, QFont::DemiBold);
    auto *text = new QVBoxLayout;
    text->setSpacing(4);
    text->addWidget(titleLabel);
    text->addWidget(descriptionLabel(description, page));
    auto *heading = new QHBoxLayout;
    heading->setContentsMargins(24, 0, 24, 0);
    heading->setSpacing(12);
    heading->addWidget(symbol);
    heading->addLayout(text, 1);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 22, 0, 0);
    layout->setSpacing(20);
    layout->addLayout(heading);
    content->setObjectName(QStringLiteral("pageContent"));
    if (!scrolls) {
        content->setContentsMargins(24, 0, 24, 20);
        layout->addWidget(content, 1);
        return page;
    }
    auto *holder = new QWidget;
    holder->setObjectName(QStringLiteral("pageContent"));
    auto *holderLayout = new QVBoxLayout(holder);
    holderLayout->setContentsMargins(24, 0, 24, 20);
    holderLayout->addWidget(content);
    holderLayout->addStretch(1);
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

void PreferencesPopup::watchChanges()
{
    for (const auto *box : findChildren<QCheckBox *>())
        connect(box, &QCheckBox::toggled, this, &PreferencesPopup::updateSummary);
    for (const auto *combo : findChildren<QComboBox *>())
        connect(combo, &QComboBox::currentIndexChanged, this, &PreferencesPopup::updateSummary);
    for (const auto *spin : findChildren<QSpinBox *>())
        connect(spin, &QSpinBox::valueChanged, this, &PreferencesPopup::updateSummary);
    for (const auto *spin : findChildren<QDoubleSpinBox *>())
        connect(spin, &QDoubleSpinBox::valueChanged, this, &PreferencesPopup::updateSummary);
    for (const auto *edit : findChildren<QLineEdit *>())
        connect(edit, &QLineEdit::textChanged, this, &PreferencesPopup::updateSummary);
    connect(themes_, &QButtonGroup::buttonToggled, this, &PreferencesPopup::updateSummary);
    const auto *folders = terminalPage_->folderList()->model();
    connect(folders, &QAbstractItemModel::dataChanged, this, &PreferencesPopup::updateSummary);
    connect(folders, &QAbstractItemModel::rowsInserted, this, &PreferencesPopup::updateSummary);
    connect(folders, &QAbstractItemModel::rowsRemoved, this, &PreferencesPopup::updateSummary);
    connect(folders, &QAbstractItemModel::rowsMoved, this, &PreferencesPopup::updateSummary);
}

void PreferencesPopup::updateSummary()
{
    if (loading_)
        return;
    const bool pending = preferences() != baseline_ || overrides_ != baselineOverrides_;
    summary_->setText(pending ? tr("Unsaved changes") : tr("No pending changes"));
    if (summary_->property("pending").toBool() != pending) {
        summary_->setProperty("pending", pending);
        summary_->style()->unpolish(summary_);
        summary_->style()->polish(summary_);
    }
    apply_->setEnabled(pending);
}

void PreferencesPopup::applyChanges()
{
    emit applied();
    baseline_ = preferences();
    baselineOverrides_ = overrides_;
    updateSummary();
}

void PreferencesPopup::applyTheme(const Theme &theme)
{
    theme_ = theme;
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(preferencesRules)));
    header_->applyTheme(theme);
    for (auto *label : findChildren<QLabel *>()) {
        const QString icon = label->property("preferenceIcon").toString();
        if (!icon.isEmpty())
            label->setPixmap(
                UiIcons::icon(icon, theme.accentText).pixmap(UiMetrics::scaledSquare(18), devicePixelRatioF()));
    }
    if (!commandFilter_->actions().isEmpty())
        commandFilter_->actions().first()->setIcon(UiIcons::icon(QStringLiteral("search"), theme.mutedText));
    updateFontPreview();
}
