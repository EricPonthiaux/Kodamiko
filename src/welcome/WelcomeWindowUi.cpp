// SPDX-License-Identifier: GPL-3.0-only

// The parts of the welcome window, built once by its constructor.

#include "ResumeCard.h"
#include "WelcomeWindow.h"
#include "recipes/RecipeLibrary.h"
#include "ui/AppFonts.h"
#include "ui/BrandHero.h"
#include "ui/BrandMark.h"
#include "ui/UiIcons.h"
#include "ui/WindowControls.h"

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include <utility>

namespace {

constexpr int titleMarkSize = 18;
// A thin bar: the welcome has no menus, only its name and the close button.
constexpr int titleBarHeight = 34;
constexpr QSize closeButtonSize{32, 24};
constexpr int firstStepsIconSize = 30;
constexpr int searchWidth = 190;
// The hero and the projects share the width 48 to 52.
constexpr int heroStretch = 48;
constexpr int panelStretch = 52;

QLabel *monospaceLabel(const QString &text, qreal scale, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = AppFonts::codeFont(AppFonts::interfaceFont().pointSizeF() * scale);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
    label->setFont(font);
    return label;
}

// A key, or keys, then what it does: "↵ Open".
void addKeyHint(QHBoxLayout *layout, const QStringList &keys, const QString &action, QWidget *parent)
{
    for (const QString &key : keys) {
        auto *cap = new QLabel(key, parent);
        cap->setObjectName(QStringLiteral("keyCap"));
        AppFonts::setInterfaceFont(cap, 0.8, QFont::Medium);
        layout->addWidget(cap);
    }
    auto *label = new QLabel(action, parent);
    label->setObjectName(QStringLiteral("footerHint"));
    AppFonts::setInterfaceFont(label, 0.85, QFont::Normal);
    layout->addWidget(label);
    layout->addSpacing(8);
}

QWidget *keyHints(const QList<std::pair<QStringList, QString>> &hints, QWidget *parent)
{
    auto *widget = new QWidget(parent);
    auto *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    for (const auto &[keys, action] : hints)
        addKeyHint(layout, keys, action, widget);
    return widget;
}

// The languages Kodamiko's templates are in: "C++", "Java"…, once each.
QStringList templateLanguages()
{
    QStringList titles;
    for (const Recipe &recipe : RecipeLibrary::available(RecipeLibrary::defaultSources())) {
        const QString title = recipe.language.isEmpty() ? QString() : ProjectLanguages::title(recipe.language);
        if (!title.isEmpty() && !titles.contains(title))
            titles.append(title);
    }
    return titles;
}

} // namespace

QWidget *WelcomeWindow::createTitleBar()
{
    auto *bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("welcomeTitleBar"));
    bar->installEventFilter(this);

    auto *mark = new QLabel(bar);
    mark->setPixmap(BrandMark::markPixmap(titleMarkSize, theme_, devicePixelRatioF()));
    auto *name = new QLabel(QStringLiteral("Kodamiko"), bar);
    AppFonts::setInterfaceFont(name, 0.92, QFont::DemiBold);
    auto *version = new QLabel(QCoreApplication::applicationVersion(), bar);
    version->setObjectName(QStringLiteral("welcomeVersion"));
    version->setFont(AppFonts::codeFont(AppFonts::interfaceFont().pointSizeF() * 0.7));
    auto *identity = new QHBoxLayout;
    identity->setSpacing(6);
    identity->addWidget(mark);
    identity->addWidget(name);
    identity->addWidget(version, 0, Qt::AlignVCenter);
    identity->addStretch();

    auto *caption = new QLabel(tr("A little space for your next big idea."), bar);
    caption->setObjectName(QStringLiteral("welcomeCaption"));
    AppFonts::setInterfaceFont(caption, 0.85, QFont::Normal);

    // Kodamiko's frame has no title bar: a close button of its own.
    close_ = new WindowButton(WindowButton::Kind::Close, bar);
    close_->applyTheme(theme_);
    close_->setCorners(WindowButton::Corners::Rounded);
    close_->setToolTip(tr("Close"));
    close_->setFixedSize(closeButtonSize);
    connect(close_, &QAbstractButton::clicked, this, &QWidget::close);

    // Sides of the same width, so that the caption is centred.
    bar->setFixedHeight(titleBarHeight);
    auto *layout = new QGridLayout(bar);
    layout->setContentsMargins(12, 0, 5, 0);
    layout->addLayout(identity, 0, 0);
    layout->addWidget(caption, 0, 1, Qt::AlignCenter);
    layout->addWidget(close_, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(2, 1);
    return bar;
}

QWidget *WelcomeWindow::createWelcomePage()
{
    auto *page = new QWidget(this);
    auto *hero = new BrandHero(theme_, page);
    hero->installEventFilter(this);

    // The panel stands off the window's edges, a line on its left only.
    auto *panelArea = new QWidget(page);
    panelArea->installEventFilter(this);
    auto *panelLayout = new QVBoxLayout(panelArea);
    panelLayout->setContentsMargins(0, 18, 22, 16);
    panelLayout->addWidget(createProjectsPanel());

    // A widget rather than a layout nested in the page's: a nested layout
    // keeps the minimum height it had before the panel's parts were hidden,
    // and the window grew to it.
    auto *sides = new QWidget(page);
    auto *sidesLayout = new QHBoxLayout(sides);
    sidesLayout->setContentsMargins(0, 0, 0, 0);
    sidesLayout->setSpacing(0);
    sidesLayout->addWidget(hero, heroStretch);
    sidesLayout->addWidget(panelArea, panelStretch);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(sides, 1);
    layout->addWidget(createFooter());
    addShortcuts(page);
    return page;
}

QWidget *WelcomeWindow::createProjectsPanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("welcomePanel"));
    panel->installEventFilter(this);

    card_ = new ResumeCard(theme_, panel);
    card_->installEventFilter(this);
    card_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card_, &QAbstractButton::clicked, this, [this] { open(card_->project().path); });
    connect(card_, &QWidget::customContextMenuRequested, this, &WelcomeWindow::showCardMenu);

    configureList();
    noMatch_ = new QLabel(tr("No matching project. Try another name."), panel);
    noMatch_->setObjectName(QStringLiteral("footerHint"));
    noMatch_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    firstSteps_ = createFirstSteps();

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(24, 12, 2, 2);
    layout->setSpacing(0);
    layout->addWidget(createPanelHeading());
    layout->addSpacing(14);
    layout->addWidget(card_);
    layout->addSpacing(12);
    layout->addWidget(createListHeading());
    layout->addSpacing(4);
    layout->addWidget(list_, 1);
    layout->addWidget(noMatch_, 1);
    layout->addWidget(firstSteps_, 1);
    layout->addSpacing(12);
    layout->addWidget(createProjectButtons());
    return panel;
}

QWidget *WelcomeWindow::createPanelHeading()
{
    auto *heading = new QWidget(this);
    eyebrow_ = monospaceLabel(QString(), 0.72, heading);
    eyebrow_->setObjectName(QStringLiteral("welcomeEyebrow"));
    title_ = new QLabel(heading);
    AppFonts::setInterfaceFont(title_, 1.9, QFont::Normal);
    // For a first launch only.
    description_ = new QLabel(tr("Big ideas welcome. Small beginnings, too."), heading);
    description_->setObjectName(QStringLiteral("footerHint"));
    AppFonts::setInterfaceFont(description_, 0.92, QFont::Normal);

    auto *layout = new QVBoxLayout(heading);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(eyebrow_);
    layout->addWidget(title_);
    layout->addWidget(description_);
    return heading;
}

QWidget *WelcomeWindow::createListHeading()
{
    listHeading_ = new QWidget(this);
    auto *title = new QLabel(tr("Recent projects"), listHeading_);
    AppFonts::setInterfaceFont(title, 0.92, QFont::DemiBold);
    count_ = monospaceLabel(QString(), 0.75, listHeading_);
    count_->setObjectName(QStringLiteral("welcomeCount"));

    search_->setObjectName(QStringLiteral("welcomeSearch"));
    search_->setPlaceholderText(tr("Find a project…"));
    search_->setClearButtonEnabled(true);
    search_->addAction(UiIcons::icon(QStringLiteral("search"), theme_.mutedText), QLineEdit::LeadingPosition);
    search_->setMaximumWidth(searchWidth);
    search_->setToolTip(tr("Search the projects (/)"));
    search_->installEventFilter(this);
    connect(search_, &QLineEdit::textChanged, this, &WelcomeWindow::filter);

    auto *layout = new QHBoxLayout(listHeading_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(title);
    layout->addWidget(count_);
    layout->addStretch();
    layout->addWidget(search_, 1);
    return listHeading_;
}

QWidget *WelcomeWindow::createFirstSteps()
{
    auto *steps = new QWidget(this);
    auto *icon = new QLabel(steps);
    icon->setPixmap(UiIcons::icon(QStringLiteral("code"), theme_.accent)
                        .pixmap(QSize(firstStepsIconSize, firstStepsIconSize), devicePixelRatioF()));
    auto *number = monospaceLabel(QStringLiteral("01"), 2.6, steps);
    number->setObjectName(QStringLiteral("welcomeNumber"));
    auto *top = new QHBoxLayout;
    top->addWidget(icon);
    top->addStretch();
    top->addWidget(number);
    auto *rule = new QFrame(steps);
    rule->setObjectName(QStringLiteral("welcomeRule"));
    rule->setFixedHeight(1);

    auto *title = new QLabel(tr("Every great project\nstarts with a first line."), steps);
    AppFonts::setInterfaceFont(title, 1.5, QFont::Normal);
    auto *text = new QLabel(tr("Start with a template, or bring a folder.\nKodamiko sets up the tools its "
                               "languages need."),
                            steps);
    text->setObjectName(QStringLiteral("footerHint"));
    auto *chips = new QHBoxLayout;
    chips->setSpacing(6);
    for (const QString &language : templateLanguages()) {
        auto *chip = monospaceLabel(language, 0.75, steps);
        chip->setObjectName(QStringLiteral("templateChip"));
        chips->addWidget(chip);
    }
    chips->addStretch();

    auto *layout = new QVBoxLayout(steps);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(10);
    layout->addLayout(top);
    layout->addWidget(rule);
    layout->addSpacing(4);
    layout->addWidget(title);
    layout->addWidget(text);
    layout->addSpacing(4);
    layout->addLayout(chips);
    layout->addStretch();
    return steps;
}

QWidget *WelcomeWindow::createProjectButtons()
{
    auto *buttons = new QWidget(this);
    newButton_ = new QPushButton(UiIcons::icon(QStringLiteral("plus"), theme_.accentText), tr("New Project…"), buttons);
    newButton_->setObjectName(QStringLiteral("welcomeAction"));
    newButton_->setDefault(true);
    newButton_->setToolTip(tr("Create a project from a template (N)"));
    connect(newButton_, &QPushButton::clicked, this, &WelcomeWindow::showNewProject);
    auto *import =
        new QPushButton(UiIcons::icon(QStringLiteral("folder"), theme_.foreground), tr("Import Project…"), buttons);
    import->setObjectName(QStringLiteral("welcomeAction"));
    import->setToolTip(tr("Add a folder you already have (O)"));
    connect(import, &QPushButton::clicked, this, &WelcomeWindow::importProject);

    auto *layout = new QHBoxLayout(buttons);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(newButton_, 1);
    layout->addWidget(import, 1);
    return buttons;
}

QWidget *WelcomeWindow::createFooter()
{
    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("welcomeFooter"));
    footer->installEventFilter(this);
    auto *about = new QPushButton(tr("About Kodamiko"), footer);
    about->setObjectName(QStringLiteral("welcomeLink"));
    AppFonts::setInterfaceFont(about, 0.85, QFont::Normal);
    connect(about, &QPushButton::clicked, this, &WelcomeWindow::showAbout);
    auto *tagline = new QLabel(tr("Your editor. Your agent. In sync."), footer);
    tagline->setObjectName(QStringLiteral("footerHint"));
    AppFonts::setInterfaceFont(tagline, 0.85, QFont::Normal);

    navigationHints_ = keyHints(
        {{{QStringLiteral("↑"), QStringLiteral("↓")}, tr("Move")}, {{QStringLiteral("↵")}, tr("Open")}}, footer);
    QWidget *creationHints =
        keyHints({{{QStringLiteral("N")}, tr("New")}, {{QStringLiteral("O")}, tr("Import")}}, footer);
    auto *hints = new QHBoxLayout;
    hints->setSpacing(0);
    hints->addStretch();
    hints->addWidget(navigationHints_);
    hints->addWidget(creationHints);

    // Sides of the same width, so that the tagline is centred.
    auto *layout = new QGridLayout(footer);
    layout->setContentsMargins(12, 6, 10, 6);
    layout->addWidget(about, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(tagline, 0, 1, Qt::AlignCenter);
    layout->addLayout(hints, 0, 2);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(2, 1);
    return footer;
}

void WelcomeWindow::addShortcuts(QWidget *page)
{
    // Single keys, as in the footer: a search being typed keeps its letters.
    const QList<std::pair<QKeySequence, std::function<void()>>> shortcuts = {
        {QKeySequence(Qt::Key_N), [this] { showNewProject(); }},
        {QKeySequence(Qt::Key_O), [this] { importProject(); }},
        {QKeySequence(Qt::Key_Slash),
         [this] {
             search_->setFocus();
             search_->selectAll();
         }},
    };
    for (const auto &[keys, action] : shortcuts) {
        auto *shortcut = new QShortcut(keys, page);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, action);
    }
}
