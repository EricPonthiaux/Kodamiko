// SPDX-License-Identifier: GPL-3.0-only

#include "NewProjectForm.h"

#include "agent/vendor/claude/ClaudeTrust.h"
#include "git/GitOperations.h"
#include "projects/ProjectStore.h"
#include "recipes/ProjectCreation.h"
#include "recipes/RecipeLibrary.h"
#include "ui/Paragraph.h"
#include "ui/PopupFooter.h"
#include "ui/PopupMetrics.h"
#include "ui/PopupWidgets.h"
#include "workspace/ChoicePopup.h"

#include <QCheckBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {

constexpr int descriptionRole = Qt::UserRole + 1;

// The choices of the last project created, proposed for the next one.
constexpr auto locationKey = "newProject/location";
constexpr auto recipeKey = "newProject/recipe";
constexpr auto gitKey = "newProject/git";

QLabel *fieldLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return label;
}

} // namespace

NewProjectForm::NewProjectForm(QWidget *parent)
    : QWidget(parent)
    , header_(new PopupHeader(QStringLiteral("plus"), tr("New Project"), this))
    , name_(new QLineEdit(this))
    , location_(new QLineEdit(this))
    , recipeList_(new QListWidget(this))
    , description_(new QLabel(this))
    , git_(new QCheckBox(tr("Create a Git repository"), this))
{
    connect(header_, &PopupHeader::closeRequested, this, &NewProjectForm::cancelled);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(createBody(), 1);
    layout->addWidget(createFooter());
    reset();
}

QWidget *NewProjectForm::createBody()
{
    auto *body = new QWidget(this);
    body->setObjectName(QStringLiteral("pageContent"));

    name_->setPlaceholderText(tr("my-project"));
    location_->setText(QSettings().value(QLatin1String(locationKey), QDir::homePath()).toString());
    auto *browse = new QPushButton(tr("Browse…"), body);
    connect(browse, &QPushButton::clicked, this, &NewProjectForm::browse);

    recipeList_->setObjectName(QStringLiteral("projectRecipes"));
    recipeList_->installEventFilter(this);
    const QString lastRecipe = QSettings().value(QLatin1String(recipeKey)).toString();
    recipes_ = RecipeLibrary::available(RecipeLibrary::defaultSources());
    for (const Recipe &recipe : std::as_const(recipes_)) {
        auto *item = new QListWidgetItem(recipe.title, recipeList_);
        item->setData(descriptionRole, recipe.description);
        if (recipe.id == lastRecipe || !recipeList_->currentItem())
            recipeList_->setCurrentItem(item);
    }
    description_->setObjectName(QStringLiteral("settingState"));
    git_->setChecked(QSettings().value(QLatin1String(gitKey), true).toBool());

    for (const QLineEdit *field : {name_, location_})
        connect(field, &QLineEdit::textChanged, this, &NewProjectForm::updateState);
    connect(recipeList_, &QListWidget::currentItemChanged, this, &NewProjectForm::updateState);
    connect(name_, &QLineEdit::returnPressed, this, &NewProjectForm::create);

    auto *grid = new QGridLayout(body);
    grid->setContentsMargins(PopupMetrics::bodyMargins);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    grid->addWidget(fieldLabel(tr("Name"), body), 0, 0);
    grid->addWidget(name_, 0, 1, 1, 2);
    grid->addWidget(fieldLabel(tr("Location"), body), 1, 0);
    grid->addWidget(location_, 1, 1);
    grid->addWidget(browse, 1, 2);
    grid->addWidget(fieldLabel(tr("Start with"), body), 2, 0, Qt::AlignTop);
    grid->addWidget(recipeList_, 2, 1, 1, 2);
    grid->addWidget(description_, 3, 1, 1, 2);
    grid->addWidget(git_, 4, 1, 1, 2);
    grid->setRowStretch(2, 1);
    grid->setColumnStretch(1, 1);
    return body;
}

QWidget *NewProjectForm::createFooter()
{
    auto *footer = new PopupFooter(this);
    summary_ = footer->hint();
    auto *cancel = new QPushButton(tr("Cancel"), footer);
    create_ = new QPushButton(tr("Create"), footer);
    create_->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &NewProjectForm::cancelled);
    connect(create_, &QPushButton::clicked, this, &NewProjectForm::create);
    footer->addButton(cancel);
    footer->addButton(create_);
    return footer;
}

void NewProjectForm::reset()
{
    name_->clear();
    updateState();
    name_->setFocus();
}

void NewProjectForm::applyTheme(const Theme &theme)
{
    header_->applyTheme(theme);
}

bool NewProjectForm::eventFilter(QObject *watched, QEvent *event)
{
    // The list keeps the item positions of its last layout: after a style
    // sheet change (the padding of its items), the items would be painted
    // at their new height over one another. Queued: the list hears the
    // change before the new rules reach its items' size.
    if (watched == recipeList_ && event->type() == QEvent::StyleChange)
        QMetaObject::invokeMethod(recipeList_, &QListWidget::doItemsLayout, Qt::QueuedConnection);
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
    const QListWidgetItem *item = recipeList_->currentItem();
    Paragraph::setText(description_, item ? item->data(descriptionRole).toString() : QString());
    const QString why = problem();
    Paragraph::setText(summary_, why.isEmpty() ? tr("Creates %1").arg(QDir::toNativeSeparators(targetFolder())) : why);
    create_->setEnabled(why.isEmpty() && selectedRecipe());
}

const Recipe *NewProjectForm::selectedRecipe() const
{
    const int row = recipeList_->currentRow();
    return row >= 0 && row < recipes_.size() ? &recipes_.at(row) : nullptr;
}

void NewProjectForm::create()
{
    const Recipe *recipe = selectedRecipe();
    if (!recipe || !problem().isEmpty())
        return;
    const QString folder = targetFolder();
    QString error;
    if (!ProjectCreation::create(*recipe, folder, &error)) {
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
