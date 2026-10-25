// SPDX-License-Identifier: GPL-3.0-only

// MainWindow: the recipes of the open project's languages, their environment
// in the terminals, their skills and instructions in the agent's sessions,
// their prompts in the Project menu, their run configurations in the Run
// menu and their setup commands at the new project's first opening.

#include "MainWindow.h"
#include "projects/ProjectStore.h"
#include "recipes/ProjectCreation.h"
#include "recipes/RecipeLibrary.h"
#include "recipes/RecipeSetup.h"
#include "run/Runner.h"
#include "settings/Preferences.h"
#include "welcome/ProjectSettingsPopup.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QMenu>

#include <algorithm>

void MainWindow::loadProjectRecipes()
{
    QStringList missing;
    recipes_ = RecipeLibrary::recipesOf(project_, &missing);
    QStringList problems;
    for (const QString &id : std::as_const(missing))
        problems.append(tr("no recipe \"%1\" is installed").arg(id));
    recipeEnvironment_ = {};
    const QString identifier = ProjectCreation::identifierOf(QFileInfo(folder_).fileName());
    QList<RunConfiguration> configurations;
    for (const Recipe &recipe : std::as_const(recipes_)) {
        // Visual Studio's environment takes about two seconds, the first
        // time: the programs started next need it.
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        const EnvironmentAdditions own = RecipeSetup::environmentAdditions(recipe.environment, &problems);
        QGuiApplication::restoreOverrideCursor();
        const EnvironmentAdditions tools = RecipeSetup::requirementAdditions(recipe.requirements, *toolchains_);
        for (const EnvironmentAdditions &additions : {own, tools}) {
            recipeEnvironment_.variables.insert(additions.variables);
            recipeEnvironment_.pathFolders.append(additions.pathFolders);
        }
        configurations.append(recipe.runConfigurationsFor(identifier));
    }
    applyTerminalEnvironment(Preferences::load().terminalPathFolders);
    runner_->setRecipeConfigurations(configurations);
    if (!problems.isEmpty())
        showStatus(tr("Recipes of %1: %2").arg(QFileInfo(folder_).fileName(), problems.join(QStringLiteral("; "))));
}

void MainWindow::applyTerminalEnvironment(const QStringList &preferredFolders)
{
    EnvironmentAdditions additions = recipeEnvironment_;
    // The user's folders first: they choose over the recipe's.
    additions.pathFolders = preferredFolders + recipeEnvironment_.pathFolders;
    setTerminalAdditions(additions);
}

void MainWindow::addRecipesToSession(AgentSessionOptions &options) const
{
    for (const Recipe &recipe : recipes_) {
        if (const QString plugin = recipe.pluginFolder(); !plugin.isEmpty())
            options.pluginFolders.append(plugin);
        if (recipe.instructions.isEmpty())
            continue;
        if (!options.instructions.isEmpty())
            options.instructions += QStringLiteral("\n\n");
        options.instructions += recipe.instructions;
    }
}

void MainWindow::addRecipePrompts(QMenu *menu)
{
    for (const Recipe &recipe : std::as_const(recipes_)) {
        if (recipe.prompts.isEmpty())
            continue;
        QMenu *prompts = menu->addMenu(tr("%1 Prompts").arg(recipe.title));
        for (const RecipePrompt &prompt : recipe.prompts) {
            const QString text = prompt.text;
            prompts->addAction(prompt.title, this, [this, text] { typeInClaudePrompt(text, PromptEntry::Typed); });
        }
    }
}

void MainWindow::setUpNewProject()
{
    if (hasMissingTools()) {
        projectSettings()->download(project_);
        return;
    }
    runRecipeSetups();
}

void MainWindow::runRecipeSetups()
{
    const bool hasSetupCommands =
        std::ranges::any_of(recipes_, [](const Recipe &recipe) { return !recipe.setupCommands.isEmpty(); });
    if (!hasSetupCommands) {
        project_.needsRecipeSetup = false;
        ProjectStore::save(project_);
        return;
    }
    for (const Recipe &recipe : std::as_const(recipes_))
        runRecipeSetup(recipe);
}

void MainWindow::runRecipeSetup(const Recipe &recipe)
{
    const QString identifier = ProjectCreation::identifierOf(QFileInfo(folder_).fileName());
    if (const std::optional<RunConfiguration> setup = recipe.setupConfigurationFor(identifier))
        runConfiguration(*setup);
}

void MainWindow::onRecipeSetupFinished(int exitCode)
{
    if (exitCode != 0 || !project_.needsRecipeSetup)
        return; // a failed setup runs again at the next opening
    project_.needsRecipeSetup = false;
    ProjectStore::save(project_);
}
