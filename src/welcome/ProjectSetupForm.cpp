// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectSetupForm.h"

#include "LanguageChoices.h"
#include "recipes/RecipeLibrary.h"
#include "toolchains/Toolchains.h"
#include "ui/AppFonts.h"
#include "ui/Paragraph.h"
#include "ui/PopupMetrics.h"
#include "ui/PopupWidgets.h"
#include "ui/UiIcons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int familyRole = Qt::UserRole;

// The group of the runtime: what builds the code, or what runs it.
QString runtimeGroup(const QString &family)
{
    const bool builds = family == QLatin1String("cpp") || family == QLatin1String("java");
    return builds ? QObject::tr("Build") : QObject::tr("Runtime");
}

QString runtimeLabel(const QString &family)
{
    if (family == QLatin1String("java"))
        return QObject::tr("JDK");
    if (family == QLatin1String("javascript"))
        return QObject::tr("Node.js");
    if (family == QLatin1String("python"))
        return QObject::tr("Python interpreter");
    return QObject::tr("C++ standard");
}

QString runtimeDescription(const QString &family)
{
    if (family == QLatin1String("java"))
        return QObject::tr("JAVA_HOME of Maven.");
    if (family == QLatin1String("javascript"))
        return QObject::tr("What the project runs on.");
    if (family == QLatin1String("python"))
        return QObject::tr("Its packages are the ones completed.");
    return QObject::tr("For the files compile_commands.json does not cover.");
}

QComboBox *settingCombo()
{
    auto *combo = new QComboBox;
    combo->setMinimumWidth(240);
    return combo;
}

QLabel *stateLabel()
{
    auto *label = new QLabel;
    label->setObjectName(QStringLiteral("settingState"));
    label->setWordWrap(true);
    label->setMaximumWidth(320);
    return label;
}

// A list with, under it, what its value means on this machine.
QWidget *fieldWithState(QComboBox *combo, QLabel *state)
{
    auto *field = new QWidget;
    auto *layout = new QVBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(combo);
    layout->addWidget(state);
    return field;
}

// The recipes of `saved`'s enabled languages, attached to the same
// languages of `project`: a language its files do not show yet (a project
// just created) is added for its recipe.
void keepRecipes(ProjectConfig &project, const ProjectConfig &saved, const Toolchains &toolchains)
{
    for (const LanguageSetup &kept : saved.languages) {
        if (!kept.enabled || kept.recipe.isEmpty())
            continue;
        if (LanguageSetup *setup = project.language(kept.language)) {
            setup->recipe = kept.recipe;
            continue;
        }
        LanguageSetup added;
        added.language = kept.language;
        added.recipe = kept.recipe;
        toolchains.complete(added);
        project.languages.append(added);
    }
}

QScrollArea *scrolled(QWidget *content)
{
    auto *holder = new QWidget;
    holder->setObjectName(QStringLiteral("pageContent"));
    auto *layout = new QVBoxLayout(holder);
    layout->setContentsMargins(PopupMetrics::bodyMargins);
    layout->addWidget(content);
    layout->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("pageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(holder);
    return scroll;
}

} // namespace

ProjectSetupForm::ProjectSetupForm(Toolchains *toolchains, QWidget *parent)
    : QWidget(parent)
    , toolchains_(toolchains)
    , stack_(new QStackedWidget(this))
{
    for (const QString &family : ProjectLanguages::families()) {
        Page page;
        page.family = family;
        stack_->addWidget(scrolled(createPage(page)));
        pages_.append(page);
    }
    for (const Page &page : std::as_const(pages_))
        connectPage(page);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createSidebar());
    layout->addWidget(stack_, 1);
}

QWidget *ProjectSetupForm::createSidebar()
{
    auto *sidebar = new QFrame(this);
    sidebar->setObjectName(QStringLiteral("popupSidebar"));
    // "JavaScript/TypeScript · Off" whole.
    sidebar->setFixedWidth(PopupMetrics::sidebarWidth);
    auto *heading = new QLabel(tr("LANGUAGES"), sidebar);
    heading->setObjectName(QStringLiteral("navHeading"));
    AppFonts::setInterfaceFont(heading, 0.85, QFont::DemiBold, 0.6);
    list_ = new QListWidget(sidebar);
    list_->setObjectName(QStringLiteral("popupNav"));
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(list_, &QListWidget::currentItemChanged, this, [this](const QListWidgetItem *current) {
        if (current)
            selectLanguage(current->data(familyRole).toString());
    });
    detect_ = new QPushButton(tr("Detect Again"), sidebar);
    detect_->setToolTip(tr("Looks at the project's files again; nothing is saved before Apply"));
    connect(detect_, &QPushButton::clicked, this, &ProjectSetupForm::detectAgain);

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(PopupMetrics::sidebarMargins);
    layout->setSpacing(6);
    layout->addWidget(heading);
    layout->addWidget(list_, 1);
    layout->addWidget(detect_, 0, Qt::AlignLeft);
    return sidebar;
}

void ProjectSetupForm::fillRecipes(const Page &page, const QString &selected)
{
    const QSignalBlocker blocker(page.recipe);
    page.recipe->clear();
    page.recipe->addItem(tr("None"), QString());
    page.recipe->setItemData(0, tr("Only the language's runtime and server."), Qt::ToolTipRole);
    for (const Recipe &recipe : RecipeLibrary::forLanguage(recipes_, page.family)) {
        page.recipe->addItem(recipe.title, recipe.id);
        page.recipe->setItemData(page.recipe->count() - 1, recipe.description, Qt::ToolTipRole);
    }
    if (!selected.isEmpty() && page.recipe->findData(selected) < 0) {
        page.recipe->addItem(tr("%1 (not installed)").arg(selected), selected);
        page.recipe->setItemData(page.recipe->count() - 1, tr("No recipe folder has this id."), Qt::ToolTipRole);
    }
    page.recipe->setCurrentIndex(std::max(page.recipe->findData(selected), 0));
}

QWidget *ProjectSetupForm::createPage(Page &page)
{
    auto *content = new QWidget;
    auto *title = new QLabel(ProjectLanguages::title(page.family), content);
    AppFonts::setInterfaceFont(title, 1.05, QFont::DemiBold);
    page.enabled = new QCheckBox(tr("Enabled"), content);
    page.enabled->setLayoutDirection(Qt::RightToLeft); // the box after its text
    auto *titleRow = new QHBoxLayout;
    titleRow->addWidget(title);
    titleRow->addStretch();
    titleRow->addWidget(page.enabled);
    page.evidence = new QLabel(content);
    page.evidence->setObjectName(QStringLiteral("pageDescription"));

    page.recipe = settingCombo();
    page.recipeState = stateLabel();
    page.runtime = settingCombo();
    page.runtimeState = stateLabel();
    page.server = settingCombo();
    page.serverVersion = settingCombo();
    page.serverState = stateLabel();
    auto *recipeCard = new SettingsCard;
    recipeCard->addRow(tr("Recipe"),
                       tr("Its environment, run configurations, skills and prompts; its files are not copied."),
                       fieldWithState(page.recipe, page.recipeState));
    auto *runtimeCard = new SettingsCard;
    runtimeCard->addRow(runtimeLabel(page.family), runtimeDescription(page.family),
                        fieldWithState(page.runtime, page.runtimeState));
    auto *serverCard = new SettingsCard;
    serverCard->addRow(tr("Language server"), tr("Completes members, imports, signatures."), page.server);
    page.versionRow = serverCard->addRow(tr("Version"), tr("Downloaded by Duet, or the system's."),
                                         fieldWithState(page.serverVersion, page.serverState));

    page.settings = new QWidget(content);
    auto *settingsLayout = new QVBoxLayout(page.settings);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(0);
    settingsLayout->addWidget(sectionHeading(tr("Recipe")));
    settingsLayout->addWidget(recipeCard);
    settingsLayout->addSpacing(16);
    settingsLayout->addWidget(sectionHeading(runtimeGroup(page.family)));
    settingsLayout->addWidget(runtimeCard);
    settingsLayout->addSpacing(16);
    settingsLayout->addWidget(sectionHeading(tr("Code intelligence")));
    settingsLayout->addWidget(serverCard);

    page.offHint = new QLabel(content);
    page.offHint->setObjectName(QStringLiteral("pageDescription"));
    Paragraph::setText(page.offHint, tr("Turn it on to choose how the project is built and completed."));

    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addLayout(titleRow);
    layout->addWidget(page.evidence);
    layout->addSpacing(14);
    layout->addWidget(page.settings);
    layout->addWidget(page.offHint);
    return content;
}

void ProjectSetupForm::connectPage(const Page &page)
{
    const QString family = page.family;
    connect(page.enabled, &QAbstractButton::toggled, this, [this, family] {
        updatePage(pageOf(family));
        updateListItem(pageOf(family));
        emit changed();
    });
    const auto update = [this, family] {
        updatePage(pageOf(family));
        emit changed();
    };
    connect(page.recipe, &QComboBox::currentIndexChanged, this, update);
    connect(page.runtime, &QComboBox::currentIndexChanged, this, update);
    connect(page.serverVersion, &QComboBox::currentIndexChanged, this, update);
    connect(page.server, &QComboBox::currentIndexChanged, this, [this, family] {
        const Page &current = pageOf(family);
        LanguageSetup setup = setupOf(current);
        setup.serverVersion.clear();
        toolchains_->complete(setup);
        fillServerVersions(current, setup.server, setup.serverVersion);
        updatePage(current);
        emit changed();
    });
}

ProjectSetupForm::Page &ProjectSetupForm::pageOf(const QString &family)
{
    const auto found = std::ranges::find(pages_, family, &Page::family);
    Q_ASSERT(found != pages_.end());
    return *found;
}

void ProjectSetupForm::applyTheme(const Theme &theme)
{
    detect_->setIcon(UiIcons::icon(QStringLiteral("refresh"), theme.foreground));
}

ProjectConfig ProjectSetupForm::detectedProject(const ProjectConfig &saved,
                                                const Toolchains &toolchains,
                                                QList<DetectedLanguage> *detected)
{
    ProjectConfig project;
    project.path = QDir(saved.path).absolutePath();
    project.name = QFileInfo(project.path).fileName();
    project.lastOpened = saved.lastOpened;
    project.needsRecipeSetup = saved.needsRecipeSetup;
    const QList<DetectedLanguage> found = ProjectDetector::detect(project.path);
    for (const DetectedLanguage &language : found) {
        LanguageSetup setup;
        setup.language = language.language;
        setup.runtimeVersion = language.runtimeVersion;
        setup.runtimePath = language.runtimePath;
        toolchains.complete(setup);
        project.languages.append(setup);
    }
    keepRecipes(project, saved, toolchains);
    if (detected)
        *detected = found;
    return project;
}

void ProjectSetupForm::setProject(const ProjectConfig &project, const QList<DetectedLanguage> &detected)
{
    project_ = project;
    recipes_ = RecipeLibrary::available(RecipeLibrary::defaultSources(project_.path));
    for (Page &page : pages_) {
        LanguageSetup setup;
        setup.language = page.family;
        setup.enabled = false;
        if (const LanguageSetup *saved = project.language(page.family))
            setup = *saved;
        toolchains_->complete(setup);
        QString evidence = tr("Not found in the project.");
        for (const DetectedLanguage &language : detected) {
            if (language.language == page.family)
                evidence = tr("Found: %1").arg(language.evidence);
        }
        if (!project.language(page.family) && detected.isEmpty() && project.configured)
            evidence = tr("Not used by the project.");
        fill(page, setup, evidence);
    }
    fillList(currentLanguage());
    emit changed();
}

void ProjectSetupForm::detectAgain()
{
    QList<DetectedLanguage> detected;
    const ProjectConfig shown = project();
    setProject(detectedProject(shown, *toolchains_, &detected), detected);
}

void ProjectSetupForm::fill(Page &page, const LanguageSetup &setup, const QString &evidence)
{
    // The switch is not blocked: it draws its knob from its own signal.
    const QSignalBlocker serverBlocker(page.server);
    Paragraph::setText(page.evidence, evidence);
    page.enabled->setChecked(setup.enabled);
    fillRecipes(page, setup.recipe);

    {
        const QSignalBlocker runtimeBlocker(page.runtime);
        LanguageChoices::fillRuntimes(page.runtime, setup, *toolchains_, project_.path);
    }

    LanguageChoices::fillServers(page.server, page.family, setup.server);
    fillServerVersions(page, setup.server, setup.serverVersion);
    updatePage(page);
}

void ProjectSetupForm::fillServerVersions(const Page &page, const QString &server, const QString &selected)
{
    const QSignalBlocker blocker(page.serverVersion);
    LanguageChoices::fillServerVersions(page.serverVersion, server, selected, *toolchains_);
}

void ProjectSetupForm::updatePage(const Page &page)
{
    page.settings->setVisible(page.enabled->isChecked());
    page.offHint->setVisible(!page.enabled->isChecked());
    page.versionRow->setVisible(!page.server->currentData().toString().isEmpty());
    page.recipeState->setText(page.recipe->currentData(Qt::ToolTipRole).toString());
    const QString runtimeState = page.runtime->currentData(LanguageChoices::stateRole).toString();
    page.runtimeState->setText(runtimeState);
    page.runtimeState->setVisible(!runtimeState.isEmpty());
    page.serverState->setText(page.serverVersion->currentData(LanguageChoices::stateRole).toString());
}

void ProjectSetupForm::fillList(const QString &keep)
{
    const QSignalBlocker blocker(list_);
    list_->clear();
    for (const bool used : {true, false}) {
        for (const Page &page : std::as_const(pages_)) {
            if (page.enabled->isChecked() != used)
                continue;
            auto *entry = new QListWidgetItem(list_);
            entry->setData(familyRole, page.family);
            entry->setSizeHint(QSize(0, 30));
            updateListItem(page);
        }
    }
    const QString shown = keep.isEmpty() ? list_->item(0)->data(familyRole).toString() : keep;
    selectLanguage(shown);
}

void ProjectSetupForm::updateListItem(const Page &page)
{
    const QString title = ProjectLanguages::title(page.family);
    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem *entry = list_->item(row);
        if (entry->data(familyRole) == page.family)
            entry->setText(page.enabled->isChecked() ? title : tr("%1 · Off").arg(title));
    }
}

QComboBox *ProjectSetupForm::recipeChoice(const QString &family)
{
    return pageOf(family).recipe;
}

QString ProjectSetupForm::currentLanguage() const
{
    const QListWidgetItem *current = list_->currentItem();
    return current ? current->data(familyRole).toString() : QString();
}

void ProjectSetupForm::selectLanguage(const QString &family)
{
    for (int row = 0; row < list_->count(); ++row) {
        if (list_->item(row)->data(familyRole) != family)
            continue;
        const QSignalBlocker blocker(list_);
        list_->setCurrentRow(row);
    }
    const auto found = std::ranges::find(pages_, family, &Page::family);
    if (found != pages_.end())
        stack_->setCurrentIndex(int(found - pages_.begin()));
}

LanguageSetup ProjectSetupForm::setupOf(const Page &page) const
{
    LanguageSetup setup;
    setup.language = page.family;
    setup.enabled = page.enabled->isChecked();
    const LanguageChoices::Runtime runtime = LanguageChoices::selectedRuntime(page.runtime);
    setup.runtimeVersion = runtime.version;
    setup.runtimePath = runtime.path;
    setup.server = page.server->currentData().toString();
    setup.serverVersion = setup.server.isEmpty() ? QString() : page.serverVersion->currentData().toString();
    setup.recipe = page.recipe->currentData().toString();
    return setup;
}

ProjectConfig ProjectSetupForm::project() const
{
    ProjectConfig project = project_;
    project.configured = true;
    project.languages.clear();
    for (const Page &page : pages_) {
        // The languages the project does not use are not kept.
        const LanguageSetup setup = setupOf(page);
        if (setup.enabled || project_.language(page.family))
            project.languages.append(setup);
    }
    return project;
}
