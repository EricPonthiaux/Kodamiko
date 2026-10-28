// SPDX-License-Identifier: GPL-3.0-only

#include "WelcomeWindow.h"

#include "NewProjectForm.h"
#include "ProjectListDelegate.h"
#include "ProjectSetupForm.h"
#include "ProjectSetupPanel.h"
#include "projects/ProjectStore.h"
#include "recipes/RecipeLibrary.h"
#include "settings/Preferences.h"
#include "toolchains/InstallPlan.h"
#include "toolchains/Toolchains.h"
#include "ui/AboutPopup.h"
#include "ui/AppFonts.h"
#include "ui/BrandMark.h"
#include "ui/ContextMenus.h"
#include "ui/PopupWidgets.h"
#include "ui/ThemeStyle.h"
#include "ui/TitleBar.h"
#include "ui/UiIcons.h"
#include "ui/WindowFrame.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWindow>

#include <utility>

namespace {

constexpr int windowWidth = 1152;
constexpr int windowHeight = 640;
constexpr int sideWidth = 248;
constexpr int brandMarkSize = 44;
constexpr int emptyMarkSize = 72;
constexpr int emptyTextWidth = 380;

constexpr const char *welcomeRules = R"(
QWidget#welcomeSide { background: @panel; border-right: 1px solid @line; }
QLabel#welcomeVersion { color: @muted; border: 1px solid @line; border-radius: 8px; padding: 0 7px; }
QWidget#welcomeSide QPushButton { text-align: left; padding: 7px 12px; border-radius: 6px; }
QWidget#welcomeEmpty QPushButton { padding: 7px 18px; border-radius: 6px; }
QWidget#welcomeSide QPushButton#welcomeLink { background: transparent; border: none; color: @muted; padding: 6px 8px; }
QWidget#welcomeSide QPushButton#welcomeLink:hover { background: @hover; color: @text; }
QFrame#welcomeLinks, QFrame#welcomeKeyHints { border: none; border-top: 1px solid @line; }
QLineEdit#welcomeSearch { padding: 6px 10px; border-radius: 6px; }
QListWidget#welcomeProjects { background: transparent; border: none; outline: none; }
QLabel#keyCap { color: @text; background: @panel; border: 1px solid @line; border-bottom: 2px solid @line; border-radius: 4px; padding: 0 5px; }
)";

using Roles = ProjectListDelegate;

bool isHeading(const QListWidgetItem *item)
{
    return item->data(Roles::headingRole).toBool();
}

QString pathOf(const QListWidgetItem *item)
{
    return item->data(Roles::projectRole).value<ProjectConfig>().path;
}

bool matches(const QListWidgetItem *item, const QString &text)
{
    return text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive) ||
           pathOf(item).contains(text, Qt::CaseInsensitive);
}

QListWidgetItem *findProject(const QListWidget &list, const QString &path)
{
    for (int i = 0; i < list.count(); ++i) {
        QListWidgetItem *item = list.item(i);
        if (!isHeading(item) && pathOf(item) == path)
            return item;
    }
    return nullptr;
}

} // namespace

WelcomeWindow::WelcomeWindow(QWidget *parent)
    : QWidget(parent)
    , toolchains_(std::make_unique<Toolchains>())
    , frame_(new WindowFrame(this))
    , pages_(new QStackedWidget(this))
    , search_(new QLineEdit(this))
    , list_(new QListWidget(this))
    , setup_(new ProjectSetupPanel(toolchains_.get(), ProjectSetupPanel::Purpose::FirstSetup, this))
    , newProject_(new NewProjectForm(this))
{
    const Preferences preferences = Preferences::load();
    theme_ = Theme::named(preferences.theme);
    preferences.applyTypography(theme_);
    ThemeStyle::apply(theme_);
    setWindowTitle(tr("Welcome to Kodamiko"));
    resize(windowWidth, windowHeight);

    pages_->addWidget(createProjectsPage());
    pages_->addWidget(setup_);
    setup_->applyTheme(theme_);
    connect(setup_, &ProjectSetupPanel::done, this, &WelcomeWindow::projectChosen);
    connect(setup_, &ProjectSetupPanel::cancelled, this, &WelcomeWindow::showProjects);
    pages_->addWidget(newProject_);
    newProject_->applyTheme(theme_);
    connect(newProject_, &NewProjectForm::cancelled, this, &WelcomeWindow::showProjects);
    // Its languages, detected from the template's files, to check.
    connect(newProject_, &NewProjectForm::created, this, &WelcomeWindow::configure);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createSide());
    layout->addWidget(pages_, 1);

    setStyleSheet(popupStyleSheet(theme_, QString::fromLatin1(welcomeRules)));
    // A square as high as the search beside it, not a title bar's control:
    // its height comes from the style sheet, so after it.
    const int searchHeight = search_->sizeHint().height();
    close_->setFixedSize(searchHeight, searchHeight);
#ifdef Q_OS_MACOS
    close_->hide();
#else
    frame_->setFrameless(!preferences.systemTitleBar);
    close_->setVisible(!preferences.systemTitleBar);
#endif
    frame_->applyTheme(theme_);

    reload();
    search_->setFocus();
}

WelcomeWindow::~WelcomeWindow() = default;

QWidget *WelcomeWindow::createSide()
{
    // Dragging the side moves the window.
    auto *side = new QWidget(this);
    side->setObjectName(QStringLiteral("welcomeSide"));
    side->setAttribute(Qt::WA_StyledBackground);
    side->setFixedWidth(sideWidth);
    side->installEventFilter(this);

    auto *logo = new QLabel(side);
    logo->setPixmap(BrandMark::markPixmap(brandMarkSize, theme_, devicePixelRatioF()));
    auto *name = new QLabel(QStringLiteral("Kodamiko"), side);
    AppFonts::setInterfaceFont(name, 1.5);
    auto *version = new QLabel(QCoreApplication::applicationVersion(), side);
    version->setObjectName(QStringLiteral("welcomeVersion"));
    AppFonts::setInterfaceFont(version, 0.85, QFont::Normal);
    auto *identity = new QVBoxLayout;
    identity->setSpacing(3);
    identity->addWidget(name);
    identity->addWidget(version, 0, Qt::AlignLeft);
    auto *brand = new QHBoxLayout;
    brand->setSpacing(12);
    brand->addWidget(logo);
    brand->addLayout(identity, 1);

    auto *hint = new QLabel(tr("Each project keeps its languages, runtimes and servers. Kodamiko downloads what they "
                               "need into its own folder."),
                            side);
    hint->setObjectName(QStringLiteral("footerHint"));
    hint->setWordWrap(true);
    AppFonts::setInterfaceFont(hint, 0.92, QFont::Normal);

    auto *links = new QFrame(side);
    links->setObjectName(QStringLiteral("welcomeLinks"));
    auto *about = new QPushButton(tr("About Kodamiko"), links);
    about->setObjectName(QStringLiteral("welcomeLink"));
    connect(about, &QPushButton::clicked, this, &WelcomeWindow::showAbout);
    auto *linksLayout = new QVBoxLayout(links);
    linksLayout->setContentsMargins(0, 10, 0, 0);
    linksLayout->addWidget(about);

    auto *layout = new QVBoxLayout(side);
    layout->setContentsMargins(24, 28, 24, 20);
    layout->setSpacing(8);
    layout->addLayout(brand);
    layout->addSpacing(28);
    addProjectButtons(layout, side);
    layout->addStretch();
    layout->addWidget(hint);
    layout->addSpacing(8);
    layout->addWidget(links);
    return side;
}

void WelcomeWindow::addProjectButtons(QBoxLayout *layout, QWidget *parent)
{
    auto *create =
        new QPushButton(UiIcons::icon(QStringLiteral("plus"), theme_.accentText), tr("New Project…"), parent);
    create->setDefault(true);
    connect(create, &QPushButton::clicked, this, &WelcomeWindow::showNewProject);
    auto *import =
        new QPushButton(UiIcons::icon(QStringLiteral("folder"), theme_.foreground), tr("Import Project…"), parent);
    connect(import, &QPushButton::clicked, this, &WelcomeWindow::importProject);
    layout->addWidget(create);
    layout->addWidget(import);
}

QWidget *WelcomeWindow::createProjectsPage()
{
    auto *projects = new QWidget(this);
    projects->installEventFilter(this);
    search_->setObjectName(QStringLiteral("welcomeSearch"));
    search_->setPlaceholderText(tr("Search projects"));
    search_->setClearButtonEnabled(true);
    search_->installEventFilter(this);
    connect(search_, &QLineEdit::textChanged, this, &WelcomeWindow::filter);
    // Kodamiko's frame has no title bar: a close button of its own.
    close_ = new WindowButton(WindowButton::Kind::Close, projects);
    close_->applyTheme(theme_);
    close_->setCorners(WindowButton::Corners::Rounded);
    close_->setToolTip(tr("Close"));
    connect(close_, &QAbstractButton::clicked, this, &QWidget::close);
    auto *top = new QHBoxLayout;
    top->setSpacing(8);
    top->addWidget(search_, 1);
    top->addStretch(); // keeps the close button on the right without the search
    top->addWidget(close_);

    list_->setObjectName(QStringLiteral("welcomeProjects"));
    const ProjectListDelegate::Actions actions{
        .showMenu = [this](const QModelIndex &index,
                           QPoint globalPosition) { showMenu(list_->item(index.row()), globalPosition); },
        .locate = [this](const QModelIndex &index) { locate(pathOf(list_->item(index.row()))); },
    };
    list_->setItemDelegate(new ProjectListDelegate(theme_, actions, list_));
    list_->setMouseTracking(true);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { open(pathOf(item)); });
    connect(list_, &QWidget::customContextMenuRequested, this, &WelcomeWindow::showContextMenu);

    emptyState_ = createEmptyState();
    keyHints_ = createKeyHints();
    auto *layout = new QVBoxLayout(projects);
    layout->setContentsMargins(32, 28, 24, 16);
    layout->setSpacing(16);
    layout->addLayout(top);
    layout->addWidget(list_, 1);
    layout->addWidget(emptyState_, 1);
    layout->addWidget(keyHints_);
    return projects;
}

QWidget *WelcomeWindow::createEmptyState()
{
    auto *empty = new QWidget(this);
    empty->setObjectName(QStringLiteral("welcomeEmpty"));
    auto *mark = new QLabel(empty);
    mark->setPixmap(BrandMark::markPixmap(emptyMarkSize, theme_, devicePixelRatioF()));
    auto *title = new QLabel(tr("Start your first project"), empty);
    AppFonts::setInterfaceFont(title, 1.3);
    auto *text = new QLabel(tr("Create one from a template, or import a folder you already have. Kodamiko sets up its "
                               "languages and downloads the tools they need."),
                            empty);
    text->setObjectName(QStringLiteral("footerHint"));
    text->setWordWrap(true);
    text->setAlignment(Qt::AlignCenter);
    text->setFixedWidth(emptyTextWidth);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addStretch();
    addProjectButtons(buttons, empty);
    buttons->addStretch();

    auto *layout = new QVBoxLayout(empty);
    layout->setSpacing(10);
    layout->addStretch();
    layout->addWidget(mark, 0, Qt::AlignHCenter);
    layout->addSpacing(6);
    layout->addWidget(title, 0, Qt::AlignHCenter);
    layout->addWidget(text, 0, Qt::AlignHCenter);
    layout->addSpacing(10);
    layout->addLayout(buttons);
    layout->addStretch();
    return empty;
}

QWidget *WelcomeWindow::createKeyHints()
{
    const QList<std::pair<QStringList, QString>> hints = {
        {{QStringLiteral("↵")}, tr("Open")},
        {{QStringLiteral("↑"), QStringLiteral("↓")}, tr("Move")},
        {{QStringLiteral("Del")}, tr("Remove from list")},
        {{QStringLiteral("⋯")}, tr("Project settings")},
    };
    auto *frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("welcomeKeyHints"));
    auto *layout = new QHBoxLayout(frame);
    layout->setContentsMargins(0, 12, 0, 0);
    layout->setSpacing(18);
    for (const auto &[keys, action] : hints) {
        auto *hint = new QHBoxLayout;
        hint->setSpacing(4);
        for (const QString &key : keys) {
            auto *cap = new QLabel(key, frame);
            cap->setObjectName(QStringLiteral("keyCap"));
            AppFonts::setInterfaceFont(cap, 0.85, QFont::Medium);
            hint->addWidget(cap);
        }
        hint->addSpacing(2);
        auto *label = new QLabel(action, frame);
        label->setObjectName(QStringLiteral("footerHint"));
        hint->addWidget(label);
        layout->addLayout(hint);
    }
    layout->addStretch();
    return frame;
}

void WelcomeWindow::reload()
{
    const QString current = list_->currentItem() ? pathOf(list_->currentItem()) : QString();
    list_->clear();
    QList<ProjectConfig> present;
    QList<ProjectConfig> missing;
    for (const ProjectConfig &project : ProjectStore::all())
        (QFileInfo(project.path).isDir() ? present : missing).append(project);
    // The projects whose folder is gone, apart at the end.
    if (!present.isEmpty() && !missing.isEmpty())
        addHeading(tr("Recent"));
    for (const ProjectConfig &project : std::as_const(present))
        addProject(project);
    if (!missing.isEmpty())
        addHeading(tr("Unavailable"));
    for (const ProjectConfig &project : std::as_const(missing))
        addProject(project);
    if (QListWidgetItem *item = findProject(*list_, current))
        list_->setCurrentItem(item);

    const bool hasProjects = !present.isEmpty() || !missing.isEmpty();
    search_->setVisible(hasProjects);
    list_->setVisible(hasProjects);
    keyHints_->setVisible(hasProjects);
    emptyState_->setVisible(!hasProjects);
    filter(search_->text());
}

void WelcomeWindow::addHeading(const QString &title)
{
    auto *item = new QListWidgetItem(title, list_);
    item->setData(Roles::headingRole, true);
    item->setFlags(Qt::NoItemFlags);
}

QListWidgetItem *WelcomeWindow::addProject(const ProjectConfig &project)
{
    auto *item = new QListWidgetItem(project.name.isEmpty() ? project.path : project.name, list_);
    item->setData(Roles::projectRole, QVariant::fromValue(project));
    item->setData(Roles::missingRole, !QFileInfo(project.path).isDir());
    item->setToolTip(QDir::toNativeSeparators(project.path));
    return item;
}

void WelcomeWindow::filter(const QString &text)
{
    QListWidgetItem *heading = nullptr;
    QListWidgetItem *firstShown = nullptr;
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem *item = list_->item(i);
        if (isHeading(item)) {
            // Hidden until a project of its group is shown.
            heading = item;
            heading->setHidden(true);
            continue;
        }
        const bool shown = matches(item, text);
        item->setHidden(!shown);
        if (shown && heading)
            heading->setHidden(false);
        if (shown && !firstShown)
            firstShown = item;
    }
    if (firstShown && (!list_->currentItem() || list_->currentItem()->isHidden()))
        list_->setCurrentItem(firstShown);
}

void WelcomeWindow::open(const QString &path)
{
    if (!QFileInfo(path).isDir())
        return;
    const std::optional<ProjectConfig> saved = ProjectStore::find(path);
    if (!saved || !saved->configured) {
        configure(path);
        return;
    }
    // Set up already: only its missing tools, if any.
    if (InstallPlan::plan(*saved, *toolchains_, RecipeLibrary::requirementsOf(*saved)).isEmpty()) {
        emit projectChosen(*saved);
        return;
    }
    setup_->load(*saved);
    pages_->setCurrentIndex(1);
    setup_->download(*saved);
}

void WelcomeWindow::configure(const QString &path)
{
    const std::optional<ProjectConfig> saved = ProjectStore::find(path);
    if (saved && saved->configured) {
        setup_->load(*saved);
    } else {
        // A project just created keeps the recipe it was made with.
        QList<DetectedLanguage> detected;
        const ProjectConfig project =
            ProjectSetupForm::detectedProject(saved.value_or(ProjectConfig{.path = path}), *toolchains_, &detected);
        setup_->load(project, detected);
    }
    pages_->setCurrentIndex(1);
}

void WelcomeWindow::showProjects()
{
    reload();
    pages_->setCurrentIndex(0);
    search_->setFocus();
}

void WelcomeWindow::showNewProject()
{
    pages_->setCurrentWidget(newProject_);
    newProject_->reset();
}

void WelcomeWindow::showAbout()
{
    if (!about_)
        about_ = new AboutPopup(theme_, this);
    about_->popup();
}

void WelcomeWindow::importProject()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Import Project"), QDir::homePath());
    if (!folder.isEmpty())
        configure(QDir(folder).absolutePath());
}

void WelcomeWindow::locate(const QString &path)
{
    const QFileInfo gone(path);
    const QString start = QFileInfo(gone.absolutePath()).isDir() ? gone.absolutePath() : QDir::homePath();
    const QString folder =
        QFileDialog::getExistingDirectory(this, tr("Locate the Folder of %1").arg(gone.fileName()), start);
    if (folder.isEmpty())
        return;
    const QString moved = QDir(folder).absolutePath();
    // The folder may be a project already: it is in the list, and the one
    // gone leaves it.
    if (!ProjectStore::relocate(path, moved))
        ProjectStore::remove(path);
    reload();
    if (QListWidgetItem *item = findProject(*list_, moved))
        list_->setCurrentItem(item);
}

void WelcomeWindow::remove(QListWidgetItem *item)
{
    if (!item || isHeading(item))
        return;
    ProjectStore::remove(pathOf(item));
    // Again, for the headings: a group may be empty now.
    reload();
}

void WelcomeWindow::showContextMenu(const QPoint &position)
{
    QListWidgetItem *item = list_->itemAt(position);
    if (item && !isHeading(item))
        showMenu(item, list_->viewport()->mapToGlobal(position));
}

void WelcomeWindow::showMenu(QListWidgetItem *item, QPoint globalPosition)
{
    const QString path = pathOf(item);
    const bool present = QFileInfo(path).isDir();
    QMenu menu(this);
    menu.addAction(tr("Open"), this, [this, path] { open(path); })->setEnabled(present);
    if (!present)
        menu.addAction(tr("Locate Folder…"), this, [this, path] { locate(path); });
    menu.addAction(tr("Project Settings…"), this, [this, path] { configure(path); })->setEnabled(present);
    menu.addSeparator();
    menu.addAction(tr("Remove from List"), this, [this, item] { remove(item); });
    ContextMenus::exec(menu, *list_, globalPosition);
}

bool WelcomeWindow::eventFilter(QObject *watched, QEvent *event)
{
    // The empty parts of the window move it, like a title bar.
    if (event->type() == QEvent::MouseButtonPress && watched != search_) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton && windowHandle() && frame_->isFrameless()) {
            windowHandle()->startSystemMove();
            return true;
        }
    }
    // From the search field, the arrows move in the list.
    if (event->type() == QEvent::KeyPress && watched == search_) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up || key->key() == Qt::Key_PageDown ||
            key->key() == Qt::Key_PageUp) {
            QApplication::sendEvent(list_, key);
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && list_->currentItem() &&
            !list_->currentItem()->isHidden()) {
            open(pathOf(list_->currentItem()));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void WelcomeWindow::keyPressEvent(QKeyEvent *event)
{
    if (pages_->currentIndex() == 0) {
        if (event->key() == Qt::Key_Delete && list_->hasFocus()) {
            remove(list_->currentItem());
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            close();
            return;
        }
    }
    if (pages_->currentWidget() == newProject_ && event->key() == Qt::Key_Escape) {
        showProjects();
        return;
    }
    QWidget::keyPressEvent(event);
}
