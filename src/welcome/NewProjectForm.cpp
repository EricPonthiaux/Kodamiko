// SPDX-License-Identifier: GPL-3.0-only

#include "NewProjectForm.h"

#include "agent/vendor/claude/ClaudeTrust.h"
#include "git/GitOperations.h"
#include "projects/ProjectStore.h"
#include "recipes/ProjectCreation.h"
#include "recipes/RecipeLibrary.h"
#include "ui/Paragraph.h"
#include "ui/PopupWidgets.h"
#include "workspace/ChoicePopup.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// The choices of the last project created, proposed for the next one.
constexpr auto locationKey = "newProject/location";
constexpr auto recipeKey = "newProject/recipe";
constexpr auto gitKey = "newProject/git";

} // namespace

NewProjectForm::NewProjectForm(QWidget *parent)
    : QWidget(parent)
    , header_(new PopupHeader(QStringLiteral("plus"), tr("New Project"), this))
    , name_(new QLineEdit(this))
    , location_(new QLineEdit(this))
    , recipeList_(new QListWidget(this))
    , recipeTitle_(new QLabel(this))
    , description_(new QLabel(this))
    , parameters_(new QWidget(this))
    , git_(new QCheckBox(tr("Initialize a Git repository"), this))
    , destination_(new QLabel(this))
{
    connect(header_, &PopupHeader::closeRequested, this, &NewProjectForm::cancelled);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(createBody(), 1);
    layout->addWidget(createFooter());
    configureFields();
    reset();
}

void NewProjectForm::configureFields()
{
    name_->setPlaceholderText(tr("my-project"));
    location_->setText(QSettings().value(QLatin1String(locationKey), QDir::homePath()).toString());
    const QString lastRecipe = QSettings().value(QLatin1String(recipeKey)).toString();
    recipes_ = RecipeLibrary::available(RecipeLibrary::defaultSources());
    for (const Recipe &recipe : std::as_const(recipes_)) {
        auto *item = new QListWidgetItem(recipe.title, recipeList_);
        item->setToolTip(recipe.description);
        if (recipe.id == lastRecipe || !recipeList_->currentItem())
            recipeList_->setCurrentItem(item);
    }
    git_->setChecked(QSettings().value(QLatin1String(gitKey), true).toBool());
    for (const QLineEdit *field : {name_, location_})
        connect(field, &QLineEdit::textChanged, this, &NewProjectForm::updateState);
    connect(recipeList_, &QListWidget::currentItemChanged, this, &NewProjectForm::updateState);
    connect(recipeList_, &QListWidget::currentItemChanged, this, &NewProjectForm::showParameters);
    connect(name_, &QLineEdit::returnPressed, this, &NewProjectForm::create);
    showParameters();
}

void NewProjectForm::reset()
{
    name_->clear();
    updateState();
    name_->setFocus();
}

bool NewProjectForm::eventFilter(QObject *watched, QEvent *event)
{
    // The list keeps the item positions of its last layout: after a style
    // sheet change (the padding of its items), the items would be painted
    // at their new height over one another. Queued: the list hears the
    // change before the new rules reach its items' size.
    if (watched == recipeList_ && event->type() == QEvent::StyleChange)
        QMetaObject::invokeMethod(recipeList_, &QListWidget::doItemsLayout, Qt::QueuedConnection);
    if (watched == destination_ && event->type() == QEvent::Resize)
        updateDestination();
    // Tabbing into a field also reveals it in short windows or with large fonts.
    auto *field = qobject_cast<QWidget *>(watched);
    if (event->type() == QEvent::FocusIn && details_ && details_->widget()->isAncestorOf(field))
        details_->ensureWidgetVisible(field);
    return QWidget::eventFilter(watched, event);
}

void NewProjectForm::browse()
{
    const QString folder =
        QFileDialog::getExistingDirectory(this, tr("Location of the New Project"), location_->text());
    if (!folder.isEmpty())
        location_->setText(QDir::toNativeSeparators(folder));
}

QString NewProjectForm::targetFolder() const
{
    const QString location = QDir::fromNativeSeparators(location_->text().trimmed());
    return QDir::cleanPath(QDir(location).filePath(name_->text().trimmed()));
}

QString NewProjectForm::problem() const
{
    const QString name = name_->text().trimmed();
    if (name.isEmpty())
        return tr("Name the project.");
    const bool isPathLike = name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'));
    if (isPathLike || name == QLatin1String(".") || name == QLatin1String(".."))
        return tr("A project name cannot contain / or \\.");
    if (!QDir::isAbsolutePath(QDir::fromNativeSeparators(location_->text().trimmed())))
        return tr("Choose the folder the project goes in.");
    if (!ProjectCreation::isFreeFolder(targetFolder()))
        return tr("%1 already exists.").arg(QDir::toNativeSeparators(targetFolder()));
    return {};
}

void NewProjectForm::updateState()
{
    const Recipe *recipe = selectedRecipe();
    recipeTitle_->setText(recipe ? recipe->title : tr("No templates available"));
    Paragraph::setText(description_, recipe ? recipe->description : tr("Add a project template to get started."));
    const QString why = problem();
    const QString hint = recipe ? tr("Your workspace is ready to create.") : tr("A template is required.");
    Paragraph::setText(summary_, why.isEmpty() ? hint : why);
    summary_->setProperty("error", !why.isEmpty() && !name_->text().trimmed().isEmpty());
    summary_->style()->unpolish(summary_);
    summary_->style()->polish(summary_);
    create_->setEnabled(why.isEmpty() && recipe);
    updateDestination();
}

void NewProjectForm::updateDestination()
{
    const QString path = QDir::toNativeSeparators(targetFolder());
    const QString text = name_->text().trimmed().isEmpty() ? tr("Your project folder will appear here") : path;
    destination_->setText(destination_->fontMetrics().elidedText(text, Qt::ElideMiddle, destination_->width()));
    destination_->setToolTip(name_->text().trimmed().isEmpty() ? QString() : path);
}

const Recipe *NewProjectForm::selectedRecipe() const
{
    const int row = recipeList_->currentRow();
    return row >= 0 && row < recipes_.size() ? &recipes_.at(row) : nullptr;
}

void NewProjectForm::showParameters()
{
    // Deleted, a widget leaves its layout.
    qDeleteAll(parameters_->findChildren<QWidget *>(Qt::FindDirectChildrenOnly));
    parameterBoxes_.clear();
    const Recipe *recipe = selectedRecipe();
    const QList<RecipeParameter> parameters = recipe ? recipe->parameters : QList<RecipeParameter>();
    QWidget *previous = recipeList_;
    for (const RecipeParameter &parameter : parameters) {
        auto *box = new QComboBox(parameters_);
        for (const RecipeParameterValue &value : parameter.values)
            box->addItem(value.value);
        auto *label = new QLabel(parameter.title, parameters_);
        label->setBuddy(box);
        box->setAccessibleName(parameter.title);
        parameters_->layout()->addWidget(label);
        parameters_->layout()->addWidget(box);
        box->installEventFilter(this);
        parameterBoxes_.append(box);
        setTabOrder(previous, box);
        previous = box;
    }
    setTabOrder(previous, git_);
    parameters_->setVisible(!parameters.isEmpty());
}

QMap<QString, QString> NewProjectForm::chosenValues() const
{
    QMap<QString, QString> values;
    const Recipe *recipe = selectedRecipe();
    if (!recipe)
        return values;
    for (qsizetype i = 0; i < std::min(recipe->parameters.size(), parameterBoxes_.size()); ++i)
        values.insert(recipe->parameters.at(i).name, parameterBoxes_.at(i)->currentText());
    return values;
}

void NewProjectForm::create()
{
    const Recipe *recipe = selectedRecipe();
    if (!recipe || !problem().isEmpty())
        return;
    const QString folder = targetFolder();
    const QMap<QString, QString> values = chosenValues();
    QString error;
    if (!ProjectCreation::create(recipe->withValues(values), folder, &error)) {
        Paragraph::setText(summary_, error);
        return;
    }
    if (git_->isChecked())
        initRepository(folder);
    trustForClaude(folder);
    QSettings settings;
    settings.setValue(QLatin1String(locationKey), location_->text().trimmed());
    settings.setValue(QLatin1String(recipeKey), recipe->id);
    settings.setValue(QLatin1String(gitKey), git_->isChecked());
    ProjectConfig project = ProjectStore::touch(folder);
    if (!recipe->language.isEmpty()) {
        LanguageSetup setup;
        setup.language = recipe->language;
        setup.recipe = recipe->id;
        project.languages.append(setup);
        project.recipeValues = values;
        project.needsRecipeSetup = true;
        ProjectStore::save(project);
    }
    emit created(folder);
}

void NewProjectForm::trustForClaude(const QString &folder)
{
    // The user chose every file of the folder: Claude's first session there
    // need not ask whether to trust them.
    QString error;
    if (!ClaudeTrust::trust(folder, &error))
        showNotice({.title = tr("Folder not trusted by Claude"),
                    .location = {},
                    .message = tr("The project was created in %1, but Claude Code will ask whether to trust it: %2")
                                   .arg(QDir::toNativeSeparators(folder), error),
                    .details = {}},
                   this);
}

void NewProjectForm::initRepository(const QString &folder)
{
    // On the UI thread, unlike the other git operations: "git init" writes
    // a few files locally, in milliseconds.
    try {
        GitOperations::init(folder);
    } catch (const GitError &error) {
        // The project is there all the same: it opens without a repository.
        showNotice({.title = tr("Git repository not created"),
                    .location = {},
                    .message = tr("The project was created in %1, but \"git init\" failed: %2")
                                   .arg(QDir::toNativeSeparators(folder), error.message()),
                    .details = {}},
                   this);
    }
}
