// SPDX-License-Identifier: GPL-3.0-only

#include "WelcomeWindow.h"

#include "NewProjectForm.h"
#include "ProjectListDelegate.h"
#include "ProjectSetupForm.h"
#include "ProjectSetupPanel.h"
#include "ResumeCard.h"
#include "projects/ProjectStore.h"
#include "recipes/RecipeLibrary.h"
#include "settings/Preferences.h"
#include "toolchains/InstallPlan.h"
#include "toolchains/Toolchains.h"
#include "ui/AboutPopup.h"
#include "ui/ContextMenus.h"
#include "ui/PopupWidgets.h"
#include "ui/ThemeStyle.h"
#include "ui/WindowControls.h"
#include "ui/WindowFrame.h"

#include <QDir>
#include <QFileDialog>
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

// The welcome, as the mockup has it; a form (New Project, a project's
// setup) needs more room, which the window grows to when it shows one.
constexpr QSize welcomeSize{880, 520};
constexpr QSize formSize{904, 640};

constexpr const char *welcomeRules = R"(
QFrame#welcomeTitleBar { border: none; border-bottom: 1px solid @line; }
QLabel#welcomeVersion { color: @muted; border: 1px solid @border; border-radius: 3px; padding: 1px 5px; }
QLabel#welcomeCaption, QLabel#welcomeCount, QLabel#welcomeNumber { color: @muted; }
QLabel#welcomeNumber { color: @border; }
QLabel#welcomeEyebrow { color: @accent; }
QFrame#welcomePanel { border: none; border-left: 1px solid @line; }
QFrame#welcomeFooter { border: none; border-top: 1px solid @line; }
QFrame#welcomeRule { background: @line; border: none; }
QPushButton#welcomeLink { background: transparent; border: none; color: @muted; padding: 4px 6px; }
QPushButton#welcomeLink:hover { color: @text; }
QPushButton#welcomeAction { padding: 8px 12px; border-radius: 5px; }
QLineEdit#welcomeSearch { padding: 3px 6px; border-radius: 5px; }
QListWidget#welcomeProjects { background: transparent; border: none; outline: none; }
QLabel#templateChip { color: @muted; border: 1px solid @border; border-radius: 3px; padding: 3px 7px; }
QLabel#keyCap { color: @text; background: @panel; border: 1px solid @line; border-bottom: 2px solid @line; border-radius: 4px; padding: 0 5px; }
)";

QString welcomeStyleSheet(const Theme &theme)
{
    // popupStyleSheet replaces the other colors.
    QString rules = QString::fromLatin1(welcomeRules);
    rules.replace(QStringLiteral("@border"), theme.border.name());
    return popupStyleSheet(theme, rules);
}

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

// Shows the projects matching `text`, and the headings of their groups;
// the one at `featuredPath`, on the card, only in a search. Returns how
// many projects show.
int showMatches(const QListWidget &list, const QString &text, const QString &featuredPath)
{
    QListWidgetItem *heading = nullptr;
    int shownCount = 0;
    for (int i = 0; i < list.count(); ++i) {
        QListWidgetItem *item = list.item(i);
        if (isHeading(item)) {
            // Hidden until a project of its group is shown.
            heading = item;
            heading->setHidden(true);
            continue;
        }
        const bool isOnCard = text.isEmpty() && pathOf(item) == featuredPath;
        const bool shown = !isOnCard && matches(item, text);
        item->setHidden(!shown);
        if (!shown)
            continue;
        ++shownCount;
        if (heading)
            heading->setHidden(false);
    }
    return shownCount;
}

bool isEnter(const QKeyEvent *key)
{
    return key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
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
    resize(welcomeSize);

    configureList();
    welcomePage_ = createWelcomePage();
    pages_->addWidget(welcomePage_);
    pages_->addWidget(setup_);
    setup_->applyTheme(theme_);
    connect(setup_, &ProjectSetupPanel::done, this, &WelcomeWindow::projectChosen);
    connect(setup_, &ProjectSetupPanel::cancelled, this, &WelcomeWindow::showProjects);
    pages_->addWidget(newProject_);
    newProject_->applyTheme(theme_);
    connect(newProject_, &NewProjectForm::cancelled, this, &WelcomeWindow::showProjects);
    // Its languages, detected from the template's files, to check.
    connect(newProject_, &NewProjectForm::created, this, &WelcomeWindow::configure);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    titleBar_ = createTitleBar();
    layout->addWidget(titleBar_);
    layout->addWidget(pages_, 1);

    setStyleSheet(welcomeStyleSheet(theme_));
#ifdef Q_OS_MACOS
    close_->hide();
#else
    frame_->setFrameless(!preferences.systemTitleBar);
    close_->setVisible(!preferences.systemTitleBar);
#endif
    frame_->applyTheme(theme_);

    reload();
    focusFirstChoice();
}

WelcomeWindow::~WelcomeWindow() = default;

void WelcomeWindow::configureList()
{
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
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->installEventFilter(this);
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { open(pathOf(item)); });
    connect(list_, &QWidget::customContextMenuRequested, this, &WelcomeWindow::showContextMenu);
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

    // The one opened last, on the card.
    featuredPath_ = present.isEmpty() ? QString() : present.first().path;
    if (!present.isEmpty())
        card_->setProject(present.first());
    showProjectsState(present.size() + missing.size());
    filter(search_->text());
}

void WelcomeWindow::showProjectsState(qsizetype projectCount)
{
    const bool hasProjects = projectCount > 0;
    eyebrow_->setText(hasProjects ? tr("YOUR WORKSPACE") : tr("YOUR NEXT CHAPTER"));
    title_->setText(tr("What will you make?"));
    title_->setVisible(!hasProjects);
    description_->setVisible(!hasProjects);
    card_->setVisible(!featuredPath_.isEmpty());
    // A single project is on the card: nothing else to list or search.
    listHeading_->setVisible(projectCount > 1);
    navigationHints_->setVisible(hasProjects);
    firstSteps_->setVisible(!hasProjects);
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
    const int shownCount = showMatches(*list_, text, featuredPath_);
    count_->setText(QString::number(shownCount));
    const bool isListed = listHeading_->isVisibleTo(this);
    list_->setVisible(isListed && shownCount > 0);
    noMatch_->setVisible(isListed && shownCount == 0);
    const QListWidgetItem *current = list_->currentItem();
    QListWidgetItem *firstShown = firstShownProject();
    if (firstShown && (!current || current->isHidden()))
        list_->setCurrentItem(firstShown);
}

QListWidgetItem *WelcomeWindow::firstShownProject() const
{
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem *item = list_->item(i);
        if (!isHeading(item) && !item->isHidden())
            return item;
    }
    return nullptr;
}

void WelcomeWindow::focusFirstChoice()
{
    if (card_->isVisibleTo(this))
        card_->setFocus();
    else if (listHeading_->isVisibleTo(this))
        search_->setFocus();
    else
        newButton_->setFocus();
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
    showForm(setup_);
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
    showForm(setup_);
}

void WelcomeWindow::showProjects()
{
    reload();
    titleBar_->show();
    pages_->setCurrentWidget(welcomePage_);
    focusFirstChoice();
}

void WelcomeWindow::showForm(QWidget *page)
{
    // The forms have a header of their own, which closes them.
    titleBar_->hide();
    pages_->setCurrentWidget(page);
    const QSize grown = size().expandedTo(formSize);
    if (grown == size())
        return;
    // Grown around its centre, where the user looks.
    const QSize growth = grown - size();
    move(pos() - QPoint(growth.width() / 2, growth.height() / 2));
    resize(grown);
}

void WelcomeWindow::showNewProject()
{
    showForm(newProject_);
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

void WelcomeWindow::showCardMenu(const QPoint &position)
{
    // The card's project is in the list too, hidden.
    if (QListWidgetItem *item = findProject(*list_, featuredPath_))
        showMenu(item, card_->mapToGlobal(position));
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
    if (moveWindow(watched, event))
        return true;
    if (event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (watched == search_)
            return searchKeyPressed(key);
        if (watched == card_)
            return cardKeyPressed(key);
        if (watched == list_)
            return listKeyPressed(key);
    }
    return QWidget::eventFilter(watched, event);
}

bool WelcomeWindow::moveWindow(QObject *watched, const QEvent *event)
{
    // The empty parts of the window move it, like a title bar; the
    // controls watched for their keys keep their clicks.
    const bool isControl = watched == search_ || watched == card_ || watched == list_;
    if (event->type() != QEvent::MouseButtonPress || isControl)
        return false;
    const auto *mouse = static_cast<const QMouseEvent *>(event);
    if (mouse->button() != Qt::LeftButton || !windowHandle() || !frame_->isFrameless())
        return false;
    windowHandle()->startSystemMove();
    return true;
}

bool WelcomeWindow::searchKeyPressed(const QKeyEvent *key)
{
    const QListWidgetItem *current = list_->currentItem();
    const bool hasCurrent = current && !current->isHidden();
    if ((key->key() == Qt::Key_Down || key->key() == Qt::Key_PageDown) && hasCurrent) {
        list_->setFocus();
        return true;
    }
    if (key->key() == Qt::Key_Up && card_->isVisible()) {
        card_->setFocus();
        return true;
    }
    if (isEnter(key) && hasCurrent) {
        open(pathOf(current));
        return true;
    }
    if (key->key() == Qt::Key_Escape && !search_->text().isEmpty()) {
        search_->clear();
        return true;
    }
    return false;
}

bool WelcomeWindow::cardKeyPressed(const QKeyEvent *key)
{
    if (isEnter(key)) {
        card_->click();
        return true;
    }
    if (key->key() != Qt::Key_Down)
        return false;
    // Down the list, or to the search above an empty one.
    if (QListWidgetItem *first = firstShownProject()) {
        list_->setCurrentItem(first);
        list_->setFocus();
    } else if (search_->isVisible()) {
        search_->setFocus();
    }
    return true;
}

bool WelcomeWindow::listKeyPressed(const QKeyEvent *key)
{
    const bool isAtTop = list_->currentItem() == firstShownProject();
    if (key->key() != Qt::Key_Up || !isAtTop)
        return false;
    // Up from the first row, back to the card.
    if (card_->isVisible())
        card_->setFocus();
    else
        search_->setFocus();
    return true;
}

void WelcomeWindow::keyPressEvent(QKeyEvent *event)
{
    if (pages_->currentWidget() == welcomePage_) {
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
