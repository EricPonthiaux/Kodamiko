// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectSetupForm.h"

#include "LanguageChoices.h"
#include "recipes/RecipeLibrary.h"
#include "toolchains/InstallPlan.h"
#include "toolchains/Toolchains.h"
#include "ui/Paragraph.h"
#include "ui/PopupMetrics.h"

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
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int familyRole = Qt::UserRole;

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

void ProjectSetupForm::connectPage(const Page &page)
{
    const QString family = page.family;
    connect(page.enabled, &QAbstractButton::toggled, this, [this, family] {
        updatePage(pageOf(family));
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
        Page &current = pageOf(family);
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

ProjectConfig ProjectSetupForm::detectedProject(const ProjectConfig &saved,
                                                const Toolchains &toolchains,
                                                QList<DetectedLanguage> *detected)
{
    ProjectConfig project = saved;
    project.languages.clear();
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
    QSignalBlocker blocker(this);
    project_ = project;
    recipes_ = RecipeLibrary::available(RecipeLibrary::defaultSources(project_.path));
    for (Page &page : pages_) {
        LanguageSetup setup;
        setup.language = page.family;
        setup.enabled = false;
        if (const LanguageSetup *saved = project.language(page.family))
            setup = *saved;
        toolchains_->complete(setup);
        QString evidence = tr("Not detected in this project. You can enable it manually.");
        if (project.language(page.family))
            evidence = tr("Configured for this project.");
        for (const DetectedLanguage &language : detected) {
            if (language.language == page.family)
                evidence = tr("Detected from %1").arg(language.evidence);
        }
        fill(page, setup, evidence);
    }
    fillList(currentLanguage());
    blocker.unblock();
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
    const QSignalBlocker enabledBlocker(page.enabled);
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

void ProjectSetupForm::updatePage(Page &page)
{
    page.enabled->setText(page.enabled->isChecked() ? tr("Enabled") : tr("Disabled"));
    page.settings->setVisible(page.enabled->isChecked());
    page.offHint->setVisible(!page.enabled->isChecked());
    page.versionRow->setVisible(!page.server->currentData().toString().isEmpty());
    updateRecipe(page);
    const QString runtimeState = page.runtime->currentData(LanguageChoices::stateRole).toString();
    page.runtimeState->setText(runtimeState);
    page.runtimeState->setVisible(!runtimeState.isEmpty());
    page.serverState->setText(page.serverVersion->currentData(LanguageChoices::stateRole).toString());
    updateAvailability(page);
    updateListItem(page);
}

void ProjectSetupForm::showRecipeServer(const Page &page, const Recipe &recipe) const
{
    const auto server = recipe.servers.constFind(page.server->currentData().toString());
    const bool isSet = server != recipe.servers.cend() && !server->version.isEmpty();
    page.serverVersion->setEnabled(!isSet);
    page.serverVersion->setToolTip(isSet ? tr("Set by the recipe") : QString());
    if (!isSet)
        return;
    // The recipe's version, whether Kodamiko's catalog offers it or not.
    const QSignalBlocker blocker(page.serverVersion);
    if (page.serverVersion->findData(server->version) < 0) {
        page.serverVersion->addItem(server->version, server->version);
        const bool isInstalled = toolchains_->isInstalled(page.server->currentData().toString(), server->version);
        page.serverVersion->setItemData(page.serverVersion->count() - 1,
                                        isInstalled ? tr("✓ Downloaded") : tr("Will download"),
                                        LanguageChoices::stateRole);
    }
    page.serverVersion->setCurrentIndex(page.serverVersion->findData(server->version));
}

void ProjectSetupForm::updateRecipe(const Page &page)
{
    Paragraph::setText(page.recipeState, page.recipe->currentData(Qt::ToolTipRole).toString());
    const QString selected = page.recipe->currentData().toString();
    const auto found = std::ranges::find(recipes_, selected, &Recipe::id);
    if (found == recipes_.end()) {
        Paragraph::setText(page.recipeTags, tr("Runtime & language server"));
        page.recipeTags->show();
        page.serverVersion->setEnabled(true);
        page.serverVersion->setToolTip({});
        return;
    }
    const Recipe recipe = found->withValues(project_.recipeValues);
    showRecipeServer(page, recipe);
    QStringList tags;
    for (const RecipeRequirement &requirement : recipe.requirements) {
        const ToolSpec *tool = ToolCatalog::find(requirement.tool.tool);
        const QString title = tool ? tool->title : requirement.tool.tool;
        tags.append((title + QLatin1Char(' ') + requirement.tool.version).trimmed());
    }
    if (!recipe.runConfigurations.isEmpty())
        tags.append(tr("Run configurations"));
    if (!recipe.instructions.isEmpty() || !recipe.pluginFolder().isEmpty())
        tags.append(tr("Agent instructions"));
    tags.removeDuplicates();
    Paragraph::setText(page.recipeTags, tags.join(QStringLiteral(" · ")));
    page.recipeTags->setVisible(!tags.isEmpty());
}

void ProjectSetupForm::updateAvailability(Page &page)
{
    ProjectConfig language = project_;
    language.languages = {setupOf(page)};
    const QList<ToolRequirement> required = RecipeLibrary::requirementsOf(language);
    const QList<InstallStep> steps = InstallPlan::plan(language, *toolchains_, required);
    const QStringList unavailable = InstallPlan::unavailable(language, *toolchains_, required);
    QString state = QStringLiteral("ready");
    QString title = tr("All required tools are available");
    QString detail = tr("Ready to apply. No downloads needed.");
    QString badge = tr("Ready");
    page.status = tr("Enabled · Ready");
    if (!steps.isEmpty()) {
        state = QStringLiteral("download");
        title = steps.size() == 1 ? tr("1 tool to download") : tr("%1 tools to download").arg(steps.size());
        detail = tr("%1 · Downloads start when you apply.").arg(InstallPlan::summary(steps));
        badge = tr("On apply");
        page.status = tr("Download needed");
    }
    if (!unavailable.isEmpty()) {
        state = QStringLiteral("missing");
        title = tr("Some tools need your attention");
        detail = unavailable.join(QLatin1Char('\n')) + (steps.isEmpty() ? QString() : QLatin1Char('\n') + detail);
        badge = tr("Missing");
        page.status = tr("Tools missing");
    }
    if (!page.enabled->isChecked())
        page.status = tr("Not enabled");
    page.toolTitle->setText(title);
    Paragraph::setText(page.toolDetail, detail);
    page.toolBadge->setText(badge);
    if (page.availability->property("state").toString() != state) {
        page.availability->setProperty("state", state);
        page.availability->style()->unpolish(page.availability);
        page.availability->style()->polish(page.availability);
        page.toolBadge->style()->polish(page.toolBadge);
    }
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
            updateListItem(page);
        }
    }
    const QString shown = keep.isEmpty() ? list_->item(0)->data(familyRole).toString() : keep;
    selectLanguage(shown);
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
