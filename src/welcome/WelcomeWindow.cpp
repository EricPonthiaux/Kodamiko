#include "WelcomeWindow.h"

#include "NewProjectForm.h"
#include "ProjectSetupForm.h"
#include "ProjectSetupPanel.h"
#include "projects/ProjectStore.h"
#include "settings/Preferences.h"
#include "toolchains/InstallPlan.h"
#include "toolchains/Toolchains.h"
#include "ui/AppFonts.h"
#include "ui/BrandMark.h"
#include "ui/PopupWidgets.h"
#include "ui/ThemeStyle.h"
#include "ui/TitleBar.h"
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
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QWindow>

#include <ranges>
#include <utility>

namespace {

constexpr int pathRole = Qt::UserRole;
constexpr int languagesRole = Qt::UserRole + 1;
constexpr int missingRole = Qt::UserRole + 2;

constexpr const char *welcomeRules = R"(
QWidget#welcomeSide { background: @panel; border-right: 1px solid @line; }
QLabel#welcomeVersion { color: @muted; }
QLineEdit#welcomeSearch { padding: 4px 8px; border-radius: 3px; }
QListWidget#welcomeProjects { background: transparent; border: none; outline: none; }
)";

// A project of the list: its name, its folder below, the languages it is
// set up for on the right.
// The chips of a project in the list: its languages, or why there are none.
QStringList chipTexts(const QStringList &languages, bool missing)
{
    if (missing)
        return {QObject::tr("Folder not found")};
    if (languages.isEmpty())
        return {QObject::tr("Not set up")};
    return languages;
}

class ProjectDelegate : public QStyledItemDelegate {
public:
    ProjectDelegate(Theme theme, QObject *parent)
        : QStyledItemDelegate(parent)
        , theme_(std::move(theme))
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return {option.rect.width(), option.fontMetrics.height() * 2 + 22};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF area = QRectF(option.rect).adjusted(2, 2, -2, -2);
        if (option.state & QStyle::State_Selected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(theme_.selection);
            painter->drawRoundedRect(area, 2, 2);
        } else if (option.state & QStyle::State_MouseOver) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(theme_.hover);
            painter->drawRoundedRect(area, 2, 2);
        }
        const bool missing = index.data(missingRole).toBool();
        const QRect text = option.rect.adjusted(14, 9, -14, -9);
        const int lineHeight = option.fontMetrics.height();

        // The languages, right-aligned chips.
        const QStringList languages = index.data(languagesRole).toStringList();
        QFont small = option.font;
        small.setPointSizeF(small.pointSizeF() * 0.85);
        const QFontMetrics smallMetrics(small);
        painter->setFont(small);
        int right = text.right();
        const QStringList chips = chipTexts(languages, missing);
        for (const auto &it : std::views::reverse(chips)) {
            const int width = smallMetrics.horizontalAdvance(it) + 14;
            const QRectF chip(right - width, text.top() + (text.height() - smallMetrics.height() - 6) / 2.0, width,
                              smallMetrics.height() + 6);
            painter->setPen(QPen(theme_.line, 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(chip, 2, 2);
            painter->setPen(languages.isEmpty() || missing ? theme_.mutedText : theme_.foreground);
            painter->drawText(chip, Qt::AlignCenter, it);
            right -= width + 6;
        }

        // The name, the folder.
        const QRect names(text.left(), text.top(), right - text.left() - 12, text.height());
        QFont bold = option.font;
        bold.setWeight(QFont::DemiBold);
        painter->setFont(bold);
        painter->setPen(missing ? theme_.mutedText : theme_.foreground);
        painter->drawText(
            QRect(names.left(), names.top(), names.width(), lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(bold).elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, names.width()));
        painter->setFont(option.font);
        painter->setPen(theme_.mutedText);
        painter->drawText(QRect(names.left(), names.top() + lineHeight + 4, names.width(), lineHeight),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          option.fontMetrics.elidedText(QDir::toNativeSeparators(index.data(pathRole).toString()),
                                                        Qt::ElideMiddle, names.width()));
        painter->restore();
    }

private:
    Theme theme_;
};

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
    setWindowTitle(tr("Welcome to Duet"));
    resize(960, 640);

    // The side: Duet's name and version; dragging it moves the window.
    auto *side = new QWidget(this);
    side->setObjectName(QStringLiteral("welcomeSide"));
    side->setAttribute(Qt::WA_StyledBackground);
    side->setFixedWidth(230);
    side->installEventFilter(this);
    auto *logo = new QLabel(side);
    logo->setPixmap(BrandMark::appIcon(44, devicePixelRatioF()));
    auto *name = new QLabel(QStringLiteral("Duet"), side);
    AppFonts::setInterfaceFont(name, 1.4);
    auto *version = new QLabel(QCoreApplication::applicationVersion(), side);
    version->setObjectName(QStringLiteral("welcomeVersion"));
    auto *identity = new QVBoxLayout;
    identity->setSpacing(0);
    identity->addWidget(name);
    identity->addWidget(version);
    auto *brand = new QHBoxLayout;
    brand->setSpacing(12);
    brand->addWidget(logo);
    brand->addLayout(identity, 1);
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(20, 24, 20, 20);
    sideLayout->addLayout(brand);
    sideLayout->addStretch();
    auto *hint = new QLabel(tr("Each project keeps its languages, runtimes and servers. Duet downloads what they "
                               "need into its own folder."),
                            side);
    hint->setObjectName(QStringLiteral("footerHint"));
    hint->setWordWrap(true);
    sideLayout->addWidget(hint);

    // The projects page: search, new, import, the list.
    auto *projects = new QWidget(this);
    projects->installEventFilter(this);
    search_->setObjectName(QStringLiteral("welcomeSearch"));
    search_->setPlaceholderText(tr("Search projects"));
    search_->setClearButtonEnabled(true);
    search_->installEventFilter(this);
    connect(search_, &QLineEdit::textChanged, this, &WelcomeWindow::filter);
    auto *create = new QPushButton(tr("New Project…"), projects);
    connect(create, &QPushButton::clicked, this, &WelcomeWindow::showNewProject);
    auto *import = new QPushButton(tr("Import Project…"), projects);
    import->setDefault(true);
    connect(import, &QPushButton::clicked, this, &WelcomeWindow::importProject);
    auto *top = new QHBoxLayout;
    top->setSpacing(8);
    top->addWidget(search_, 1);
    top->addWidget(create);
    top->addWidget(import);
    // Duet's frame has no title bar: a close button of its own.
    auto *close = new WindowButton(WindowButton::Kind::Close, projects);
    close->applyTheme(theme_);
    close->setToolTip(tr("Close"));
    connect(close, &QAbstractButton::clicked, this, &QWidget::close);
    top->addWidget(close, 0, Qt::AlignTop);

    list_->setObjectName(QStringLiteral("welcomeProjects"));
    list_->setItemDelegate(new ProjectDelegate(theme_, list_));
    list_->setMouseTracking(true);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(list_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) { open(item->data(pathRole).toString()); });
    connect(list_, &QWidget::customContextMenuRequested, this, &WelcomeWindow::showContextMenu);
    auto *footer =
        new QLabel(tr("Enter to open · Delete to remove from the list · right-click for the settings"), projects);
    footer->setObjectName(QStringLiteral("footerHint"));
    auto *projectsLayout = new QVBoxLayout(projects);
    projectsLayout->setContentsMargins(24, 20, 16, 14);
    projectsLayout->setSpacing(12);
    projectsLayout->addLayout(top);
    projectsLayout->addWidget(list_, 1);
    projectsLayout->addWidget(footer);

    pages_->addWidget(projects);
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
    layout->addWidget(side);
    layout->addWidget(pages_, 1);

    setStyleSheet(popupStyleSheet(theme_, QString::fromLatin1(welcomeRules)));
#ifdef Q_OS_MACOS
    close->hide();
#else
    frame_->setFrameless(!preferences.systemTitleBar);
    close->setVisible(!preferences.systemTitleBar);
#endif
    frame_->applyTheme(theme_);

    reload();
    search_->setFocus();
}

WelcomeWindow::~WelcomeWindow() = default;

void WelcomeWindow::reload()
{
    const QString current = list_->currentItem() ? list_->currentItem()->data(pathRole).toString() : QString();
    list_->clear();
    for (const ProjectConfig &project : ProjectStore::all()) {
        auto *item = new QListWidgetItem(project.name.isEmpty() ? project.path : project.name, list_);
        item->setData(pathRole, project.path);
        item->setData(languagesRole, shownLanguages(project));
        item->setData(missingRole, !QFileInfo(project.path).isDir());
        item->setToolTip(QDir::toNativeSeparators(project.path));
        if (project.path == current)
            list_->setCurrentItem(item);
    }
    if (!list_->currentItem() && list_->count() > 0)
        list_->setCurrentRow(0);
    filter(search_->text());
}

QStringList WelcomeWindow::shownLanguages(const ProjectConfig &project)
{
    if (!project.configured)
        return {};
    QStringList languages;
    for (const LanguageSetup &setup : project.languages) {
        if (setup.enabled)
            languages.append(ProjectLanguages::title(setup.language));
    }
    if (languages.isEmpty())
        languages.append(tr("No language server"));
    return languages;
}

void WelcomeWindow::filter(const QString &text)
{
    QListWidgetItem *firstShown = nullptr;
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem *item = list_->item(i);
        const bool shown = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive) ||
                           item->data(pathRole).toString().contains(text, Qt::CaseInsensitive);
        item->setHidden(!shown);
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
    if (InstallPlan::plan(*saved, *toolchains_).isEmpty()) {
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
        QList<DetectedLanguage> detected;
        ProjectConfig project = ProjectSetupForm::detectedProject(path, *toolchains_, &detected);
        if (saved)
            project.lastOpened = saved->lastOpened;
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

void WelcomeWindow::importProject()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Import Project"), QDir::homePath());
    if (!folder.isEmpty())
        configure(QDir(folder).absolutePath());
}

void WelcomeWindow::remove(QListWidgetItem *item)
{
    if (!item)
        return;
    ProjectStore::remove(item->data(pathRole).toString());
    delete item;
}

void WelcomeWindow::showContextMenu(const QPoint &position)
{
    QListWidgetItem *item = list_->itemAt(position);
    if (!item)
        return;
    const QString path = item->data(pathRole).toString();
    const bool present = QFileInfo(path).isDir();
    QMenu menu(this);
    menu.addAction(tr("Open"), this, [this, path] { open(path); })->setEnabled(present);
    menu.addAction(tr("Project Settings…"), this, [this, path] { configure(path); })->setEnabled(present);
    menu.addSeparator();
    menu.addAction(tr("Remove from List"), this, [this, item] { remove(item); });
    menu.exec(list_->viewport()->mapToGlobal(position));
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
            open(list_->currentItem()->data(pathRole).toString());
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
