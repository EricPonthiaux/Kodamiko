// SPDX-License-Identifier: GPL-3.0-only

// The parts of the welcome window, built once by its constructor.

#include "WelcomeWindow.h"
#include "ui/AppFonts.h"
#include "ui/BrandMark.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"
#include "ui/WindowControls.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include <utility>

namespace {

// A thin bar: the welcome has its mark, name and close button, without menus.
constexpr int titleMarkSize = 18;
constexpr int searchWidth = 206;

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

} // namespace

QWidget *WelcomeWindow::createTitleBar()
{
    auto *bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("welcomeTitleBar"));
    bar->installEventFilter(this);

    auto *mark = new QLabel(bar);
    mark->setPixmap(BrandMark::markPixmap(titleMarkSize, theme_, devicePixelRatioF()));
    mark->setFixedSize(titleMarkSize, titleMarkSize);
    mark->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *name = new QLabel(QStringLiteral("Kodamiko"), bar);
    name->setObjectName(QStringLiteral("footerHint"));
    AppFonts::setInterfaceFont(name, 0.85, QFont::Medium);

    // Kodamiko's frame has no title bar: a close button of its own.
    close_ = new WindowButton(WindowButton::Kind::Close, bar);
    close_->applyTheme(theme_);
    close_->setCorners(WindowButton::Corners::Rounded);
    close_->setToolTip(tr("Close"));
    close_->setFixedSize(UiMetrics::compactCloseButtonSize);
    connect(close_, &QAbstractButton::clicked, this, &QWidget::close);

    bar->setFixedHeight(UiMetrics::compactTitleBarHeight);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(UiMetrics::compactTitleBarMargins);
    layout->setSpacing(UiMetrics::compactTitleBarSpacing);
    layout->addWidget(mark, 0, Qt::AlignVCenter);
    layout->addWidget(name, 1);
    layout->addWidget(close_, 0, Qt::AlignRight | Qt::AlignVCenter);
    return bar;
}

QWidget *WelcomeWindow::createWelcomePage()
{
    auto *page = new QWidget(this);
    auto *content = new QWidget(page);
    content->installEventFilter(this);
    auto *rule = new QFrame(content);
    rule->setObjectName(QStringLiteral("welcomeRule"));
    rule->setFixedHeight(1);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(36, 24, 36, 24);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(createProjectsPanel(), 1);
    contentLayout->addSpacing(16);
    contentLayout->addWidget(rule);
    contentLayout->addSpacing(16);
    contentLayout->addWidget(createProjectButtons(), 0, Qt::AlignRight);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(content, 1);
    layout->addWidget(createFooter());
    addShortcuts(page);
    return page;
}

QWidget *WelcomeWindow::createProjectsPanel()
{
    auto *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("welcomePanel"));
    panel->installEventFilter(this);

    noMatch_ = new QLabel(tr("No matching project. Try another name."), panel);
    noMatch_->setObjectName(QStringLiteral("welcomeNoMatch"));
    noMatch_->setAlignment(Qt::AlignCenter);
    noMatch_->setWordWrap(true);
    firstSteps_ = createFirstSteps();

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createListHeading());
    layout->addSpacing(12);
    layout->addWidget(list_, 1);
    layout->addWidget(noMatch_, 1);
    layout->addWidget(firstSteps_, 1);
    return panel;
}

QWidget *WelcomeWindow::createListHeading()
{
    auto *heading = new QWidget(this);
    auto *title = new QLabel(tr("Recent projects"), heading);
    AppFonts::setInterfaceFont(title, 1, QFont::Medium);
    count_ = new QLabel(heading);
    AppFonts::setInterfaceFont(count_, 0.85, QFont::Normal);
    count_->setObjectName(QStringLiteral("welcomeCount"));

    search_->setObjectName(QStringLiteral("welcomeSearch"));
    search_->setPlaceholderText(tr("Find a project…"));
    search_->setAccessibleName(tr("Find a project"));
    search_->setClearButtonEnabled(true);
    search_->addAction(UiIcons::icon(QStringLiteral("search"), theme_.mutedText), QLineEdit::LeadingPosition);
    search_->setMaximumWidth(searchWidth);
    search_->setToolTip(tr("Search the projects (/)"));
    search_->installEventFilter(this);
    connect(search_, &QLineEdit::textChanged, this, &WelcomeWindow::filter);

    auto *layout = new QHBoxLayout(heading);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(title);
    layout->addWidget(count_);
    layout->addStretch();
    layout->addWidget(search_, 1);
    return heading;
}

QWidget *WelcomeWindow::createFirstSteps()
{
    auto *steps = new QWidget(this);
    steps->setObjectName(QStringLiteral("welcomeEmptyState"));
    auto *title = new QLabel(tr("No recent projects"), steps);
    title->setAlignment(Qt::AlignCenter);
    AppFonts::setInterfaceFont(title, 1.1, QFont::Medium);
    auto *text = new QLabel(tr("Create a project from a template, or open an existing folder."), steps);
    text->setObjectName(QStringLiteral("footerHint"));
    text->setAlignment(Qt::AlignCenter);
    text->setWordWrap(true);
    AppFonts::setInterfaceFont(text, 0.95, QFont::Normal);

    auto *layout = new QVBoxLayout(steps);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);
    layout->addStretch();
    layout->addWidget(title);
    layout->addWidget(text);
    layout->addStretch();
    return steps;
}

QWidget *WelcomeWindow::createProjectButtons()
{
    auto *buttons = new QWidget(this);
    newButton_ = new QPushButton(UiIcons::icon(QStringLiteral("plus"), theme_.onAccent), tr("New project"), buttons);
    newButton_->setObjectName(QStringLiteral("welcomeAction"));
    newButton_->setDefault(true);
    AppFonts::setInterfaceFont(newButton_, 0.95, QFont::Medium);
    newButton_->setToolTip(tr("Create a project from a template (N)"));
    connect(newButton_, &QPushButton::clicked, this, &WelcomeWindow::showNewProject);
    auto *import =
        new QPushButton(UiIcons::icon(QStringLiteral("folder"), theme_.foreground), tr("Open folder"), buttons);
    import->setObjectName(QStringLiteral("welcomeAction"));
    AppFonts::setInterfaceFont(import, 0.95, QFont::Medium);
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
    auto *version = new QLabel(QCoreApplication::applicationVersion(), footer);
    version->setObjectName(QStringLiteral("welcomeVersion"));
    AppFonts::setInterfaceFont(version, 0.8, QFont::Normal);
    navigationHints_ = keyHints(
        {{{QStringLiteral("↑"), QStringLiteral("↓")}, tr("Move")}, {{QStringLiteral("↵")}, tr("Open")}}, footer);
    QWidget *creationHints =
        keyHints({{{QStringLiteral("N")}, tr("New")}, {{QStringLiteral("O")}, tr("Open")}}, footer);
    auto *hints = new QHBoxLayout;
    hints->setSpacing(0);
    hints->addStretch();
    hints->addWidget(navigationHints_);
    hints->addWidget(creationHints);

    auto *layout = new QHBoxLayout(footer);
    layout->setContentsMargins(16, 8, 12, 8);
    layout->addWidget(about, 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addSpacing(16);
    layout->addWidget(version);
    layout->addLayout(hints, 1);
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
